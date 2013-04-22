
import Mapper.*;
import Mapper.Device.*;
import java.util.Arrays;

class test {
    public static void main(String [] args) {
        final Device dev = new Device("javatest", 9000);

        // This is how to ensure the device is freed when the program
        // exits, even on SIGINT.  The Device must be declared "final".
        Runtime.getRuntime().addShutdownHook(new Thread()
            {
                @Override
                public void run()
                    {
                        dev.free();
                    }
            });

        Mapper.Device.Signal inp1 = dev.add_input("insig1", 1, 'f', "Hz", 2.0, null,
            new InputListener() {
                public void onInput(Mapper.Device.Signal sig,
                                    Mapper.Db.Signal props,
                                    int instance_id,
                                    float[] v,
                                    TimeTag tt) {
                    System.out.println("in onInput() for "
                                       +props.name()+": "
                                       +Arrays.toString(v));
                }});

        System.out.println("Input signal name: "+inp1.name());

        Signal out1 = dev.add_output("outsig1", 1, 'i', "Hz", 0.0, 1.0);

        System.out.println("Output signal index: "+out1.index());
        System.out.println("Zeroeth output signal name: "+dev.get_output_by_index(0).name());

        dev.set_property("width", new PropertyValue(256));
        dev.set_property("height", new PropertyValue(12.5));
        dev.set_property("depth", new PropertyValue("67"));
        dev.set_property("deletethis", new PropertyValue("should not see me"));
        dev.remove_property("deletethis");

        out1.set_property("width", new PropertyValue(128));
        out1.set_property("height", new PropertyValue(6.25));
        out1.set_property("depth", new PropertyValue("test"));
        out1.set_property("deletethis", new PropertyValue("or me"));
        out1.remove_property("deletethis");

        System.out.println("Setting name of out1 to `/out1test'.");
        out1.properties().set_name("/out1test");
        System.out.println("Name of out1: " + out1.properties().name());

        System.out.println("Looking up `height': "
                           + out1.properties().property_lookup("height"));
        System.out.println("Looking up `width': "
                           + out1.properties().property_lookup("width"));
        System.out.println("Looking up `depth': "
                           + out1.properties().property_lookup("depth"));

        System.out.println("Waiting for ready...");
        while (!dev.ready()) {
            dev.poll(100);
        }
        System.out.println("Device is ready.");

        System.out.println("Device name: "+dev.name());
        System.out.println("Device port: "+dev.port());
        System.out.println("Device ordinal: "+dev.ordinal());
        System.out.println("Device interface: "+dev.iface());
        System.out.println("Device ip4: "+dev.ip4());

        int i = 100;
        double [] ar = new double [] {0};
        TimeTag tt = new TimeTag(0,0);

        // Signal should report no value before the first update.
        if (out1.value(ar, tt))
            System.out.println("Signal has value: " + ar[0]);
        else
            System.out.println("Signal has no value.");

        // Just to test vector-valued signal and timetag support,
        out1.update(new int []{i}, TimeTag.NOW);

        while (i >= 0) {
            System.out.print("Updated value to: " + i);

            // Note, we are testing an implicit cast form int to float
            // here because we are passing a double[] into
            // out1.value().
            if (out1.value(ar, tt))
                System.out.print("  Signal has value: " + ar[0]);
            else
                System.out.print("  Signal has no value.");

            System.out.print("      \r");

            dev.poll(100);
            --i;

            out1.update(i);
        }
        dev.free();
    }
}
