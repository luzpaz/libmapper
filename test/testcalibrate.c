#include <mpr/mpr.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int terminate = 0;
int done = 0;
int period = 100;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

mpr_map map;

int sent = 0;
int received = 0;

float sMin, sMax, dMin, dMax;
float M, B, expected;

int setup_src(char *iface)
{
    src = mpr_dev_new("testcalibrate-send", 0);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(src), iface);
    eprintf("source created.\n");

    int mn=0, mx=1;
    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_INT32, NULL,
                          &mn, &mx, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");
    mpr_list l = mpr_dev_get_sigs(src, MPR_DIR_OUT);
    eprintf("Number of outputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
    return 0;

  error:
    return 1;
}

void cleanup_src()
{
    if (src) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mpr_dev_free(src);
        eprintf("ok\n");
    }
}

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (value) {
        eprintf("handler: Got %f (expected", (*(float*)value));
        if (fabs(*(float*)value - expected) < 0.0001)
            received++;
        else
            eprintf(" %f", expected);
        eprintf(")\n");
    }
}

int setup_dst(char *iface)
{
    dst = mpr_dev_new("testcalibrate-recv", 0);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created.\n");

    float mn=0, mx=1;
    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, handler, MPR_SIG_UPDATE);

    eprintf("Input signal 'insig' registered.\n");
    mpr_list l = mpr_dev_get_sigs(dst, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
    return 0;

  error:
    return 1;
}

void cleanup_dst()
{
    if (dst) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mpr_dev_free(dst);
        eprintf("ok\n");
    }
}

int setup_maps(int calibrate)
{
    char expr[128];
    if (!map)
        map = mpr_map_new(1, &sendsig, 1, &recvsig);

    if (calibrate) {
        do {
            dMin = rand() % 100;
            dMax = rand() % 100;
        } while (dMax <= dMin);
        snprintf(expr, 128, "y=linear(x,?,?,%f,%f)", dMin, dMax);
    }
    else
        snprintf(expr, 128, "y=linear(x,-,-,-,-)");   
    mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);
    mpr_obj_push(map);

    // Wait until mapping has been established
    do {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    } while (!done && !mpr_map_get_is_ready(map));

    eprintf("map initialized with expression '%s'\n",
            mpr_obj_get_prop_as_str(map, MPR_PROP_EXPR, NULL));

    return 0;
}

void wait_ready()
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
}

void loop()
{
    eprintf("Polling device..\n");
    int i = -1, val = 0, changed = 0;
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sendsig, MPR_PROP_NAME, NULL);

    while (1) {
        eprintf("Calibrating to dst range [%f, %f]\n", dMin, dMax);
        setup_maps(1);
        i = 0;
        while (i < 50 && !done) {
            mpr_dev_poll(src, 0);
            val += (rand() % 6) - 3;
            eprintf("Updating signal %s to %d\n", name, val);
            mpr_sig_set_value(sendsig, 0, 1, MPR_INT32, &val, MPR_NOW);
            if (0 == i) {
                sMin = sMax = val;
                changed = 1;
            }
            else if (val < sMin) {
                sMin = val;
                changed = 1;
            }
            else if (val > sMax) {
                sMax = val;
                changed = 1;
            }
            if (changed) {
                // (re)calculate M and B for checking generated expression
                float sRange = sMax - sMin;
                M = sRange ? (dMax - dMin) / sRange : 0;
                B = sRange ? (dMin * sMax - dMax * sMin) / sRange : dMin;
                eprintf("updated ranges: [%f,%f]->[%f,%f], M=%f, B=%f\n",
                        sMin, sMax, dMin, dMax, M, B);
                changed = 0;
            }
            expected = val * M + B;
            ++sent;
            mpr_dev_poll(dst, period);
            i++;
            if (!verbose) {
                printf("\r  Sent: %4i, Received: %4i   ", sent, received);
                fflush(stdout);
            }
        }

        // no calibration
        setup_maps(0);
        i = 0;
        while (i < 50 && !done) {
            mpr_dev_poll(src, 0);
            val = i;
            eprintf("Updating signal %s to %d\n", name, val);
            mpr_sig_set_value(sendsig, 0, 1, MPR_INT32, &val, MPR_NOW);
            expected = val * M + B;
            ++sent;
            mpr_dev_poll(dst, period);
            i++;
            if (!verbose) {
                printf("\r  Sent: %4i, Received: %4i   ", sent, received);
                fflush(stdout);
            }
        }
        if (terminate || done)
            break;
    }
}

void ctrlc(int signal)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    char *iface = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testcalibrate.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help\n");
                        return 1;
                        break;
                    case 'f':
                        period = 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 't':
                        terminate = 1;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
                            j = 1;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    if (setup_dst(iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src(iface)) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_ready();

    loop();

    if (!received || sent != received) {
        eprintf("Not all sent messages were received.\n");
        eprintf("Updated value %d time%s and received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
