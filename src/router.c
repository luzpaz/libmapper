
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

mapper_router mapper_router_new(mapper_device device, const char *host,
                                int port, const char *name, int remap_instances)
{
    char str[16];
    mapper_router router = (mapper_router) calloc(1, sizeof(struct _mapper_router));
    sprintf(str, "%d", port);
    router->remote_addr = lo_address_new(host, str);
    router->remote_name = strdup(name);
    router->device = device;
    router->outgoing = 0;
    router->remap_instances = remap_instances;
    router->instance_map = 0;

    if (!router->remote_addr) {
        mapper_router_free(router);
        return 0;
    }
    return router;
}

void mapper_router_free(mapper_router router)
{
    if (router) {
        if (router->remote_addr)
            lo_address_free(router->remote_addr);
        if (router->outgoing) {
            mapper_signal_connection sc = router->outgoing;
            while (sc) {
                mapper_signal_connection tmp = sc->next;
                if (sc->connection) {

                    mapper_connection c = sc->connection;
                    while (c) {
                        mapper_connection tmp = c->next;
                        if (tmp->props.src_name)
                            free(tmp->props.src_name);
                        if (tmp->props.dest_name)
                            free(tmp->props.dest_name);
                        free(c);
                        c = tmp;
                    }
                }
                free(sc);
                sc = tmp;
            }
        }
        if (router->instance_map) {
            mapper_instance_map map = router->instance_map;
            while (map) {
                mapper_instance_map tmp = map->next;
                free(map);
                map = tmp;
            }
        }
        free(router);
    }
}

void mapper_router_send_signal(mapper_connection_instance ci, int id)
{
    printf("mapper_router_send_signal\n");
    int i;
    lo_message m;
    if (!ci->connection->router->remote_addr)
        return;

    m = lo_message_new();
    if (!m)
        return;

    if (ci->parent->signal->props.instances > 1)
        lo_message_add_int32(m, id);

    if (ci->history.position != -1) {
        if (ci->history.type == 'f') {
            float *v = msig_history_value_pointer(ci->history);
            for (i = 0; i < ci->history.length; i++)
                lo_message_add_float(m, v[i]);
        }
        else if (ci->history.type == 'i') {
            int *v = msig_history_value_pointer(ci->history);
            for (i = 0; i < ci->history.length; i++)
                lo_message_add_int32(m, v[i]);
        }
        else if (ci->history.type == 'd') {
            double *v = msig_history_value_pointer(ci->history);
            for (i = 0; i < ci->history.length; i++)
                lo_message_add_double(m, v[i]);
        }
    }
    else {
        lo_message_add_nil(m);
    }

    lo_send_message_from(ci->connection->router->remote_addr,
                         ci->connection->router->device->server,
                         ci->connection->props.dest_name,
                         m);
    lo_message_free(m);
    return;
}

int mapper_router_send_query(mapper_router router, mapper_signal sig,
                             const char *alias)
{
    // find this signal in list of connections
    mapper_signal_connection sc = router->outgoing;
    while (sc && sc->signal != sig)
        sc = sc->next;

    // exit without failure if signal is not mapped
    if (!sc) {
        return 0;
    }
    // for each connection, query the remote signal
    mapper_connection c = sc->connection;
    int count = 0;
    char query_string[1024];
    while (c) {
        strncpy(query_string, c->props.dest_name, 1024);
        strncat(query_string, "/get", 4);
        if (alias) {
            lo_send_from(router->remote_addr, router->device->server, 
                         LO_TT_IMMEDIATE, query_string, "s", alias);
        }
        else {
            lo_send_from(router->remote_addr, router->device->server, 
                         LO_TT_IMMEDIATE, query_string, "");
        }
        count++;
        c = c->next;
    }
    return count;
}

mapper_connection mapper_router_add_connection(mapper_router router,
                                               mapper_signal sig,
                                               const char *dest_name,
                                               char dest_type,
                                               int dest_length)
{
    /* Currently, fail if lengths don't match.  TODO: In the future,
     * we'll have to examine the expression to see if its input and
     * output lengths are compatible. */
    if (sig->props.length != dest_length) {
        char n[1024];
        msig_full_name(sig, n, 1024);
        trace("rejecting connection %s -> %s%s because lengths "
              "don't match (not yet supported)\n",
              n, router->remote_name, dest_name);
        return 0;
    }

    mapper_connection connection = (mapper_connection)
        calloc(1, sizeof(struct _mapper_connection));
    
    connection->props.src_name = strdup(sig->props.name);
    connection->props.src_type = sig->props.type;
    connection->props.src_length = sig->props.length;
    connection->props.dest_name = strdup(dest_name);
    connection->props.dest_type = dest_type;
    connection->props.dest_length = dest_length;
    connection->props.mode = MO_UNDEFINED;
    connection->props.expression = strdup("y=x");
    connection->props.clip_min = CT_NONE;
    connection->props.clip_max = CT_NONE;
    connection->props.muted = 0;
    connection->source = sig;
    connection->router = router;

    // create connection instances as necessary
    mapper_signal_instance si = sig->active;
    while (si) {
        msig_add_connection_instance(si, connection);
        si = si->next;
    }

    // do the same for reserved instances
    si = sig->reserve;
    while (si) {
        msig_add_connection_instance(si, connection);
        si = si->next;
    }

    // find signal in signal connection list
    mapper_signal_connection sc = router->outgoing;
    while (sc && sc->signal != sig)
        sc = sc->next;

    // if not found, create a new list entry
    if (!sc) {
        sc = (mapper_signal_connection)
            calloc(1, sizeof(struct _mapper_signal_connection));
        sc->signal = sig;
        sc->next = router->outgoing;
        router->outgoing = sc;
    }
    // add new connection to this signal's list
    connection->next = sc->connection;
    sc->connection = connection;
    router->device->n_connections++;
    
    return connection;
}

int mapper_router_remove_connection(mapper_router router, 
                                    mapper_connection connection)
{
    // remove associated connection instances
    mapper_signal_instance si = connection->source->active;
    while (si) {
        mapper_connection_instance temp, *ci = &si->connections;
        while (*ci) {
            if ((*ci)->connection == connection) {
                temp = *ci;
                *ci = (*ci)->next;
                msig_free_connection_instance(temp);
                break;
            }
            ci = &(*ci)->next;
        }
        si = si->next;
    }
    // do the same for reserved instances of this signal
    si = connection->source->reserve;
    while (si) {
        mapper_connection_instance temp, *ci = &si->connections;
        while (*ci) {
            if ((*ci)->connection == connection) {
                temp = *ci;
                *ci = (*ci)->next;
                msig_free_connection_instance(temp);
                break;
            }
            ci = &(*ci)->next;
        }
        si = si->next;
    }
    // find signal in signal connection list
    mapper_signal_connection sc = router->outgoing;
    while (sc) {
        mapper_connection *c = &sc->connection;
        while (*c) {
            if (*c == connection) {
                *c = connection->next;
                free(connection);
                router->device->n_connections--;
                return 0;
            }
            c = &(*c)->next;
        }
        sc = sc->next;
    }
    return 1;
}

void mapper_router_set_id_map(mapper_router router, int local, int remote)
{
    mapper_instance_map map = router->instance_map;
    while (map) {
        if (map->local == local) {
            map->remote = remote;
            return;
        }
        map = map->next;
    }

    // map not found, create it
    printf("writing id map: %i -> %i\n", remote, local);
    map = (mapper_instance_map)calloc(1, sizeof(mapper_instance_map));
    map->local = local;
    map->remote = remote;
    map->next = router->instance_map;
    router->instance_map = map;
    printf("    ID MAP:\n");
    map = router->instance_map;
    while (map) {
        printf("    %i -> %i\n", map->remote, map->local);
        map = map->next;
    }
}

int mapper_router_get_local_id_map(mapper_router router, int local, int *remote)
{
    mapper_instance_map map = router->instance_map;
    while (map) {
        if (map->local == local) {
            *remote = map->remote;
            return 0;
        }
        map = map->next;
    }
    return 1;
}

int mapper_router_get_remote_id_map(mapper_router router, int remote, int *local)
{
    mapper_instance_map map = router->instance_map;
    while (map) {
        if (map->remote == remote) {
            *local = map->local;
            return 0;
        }
        map = map->next;
    }
    return 1;
}

mapper_router mapper_router_find_by_remote_address(mapper_router router,
                                                   lo_address address)
{
    const char *host = lo_address_get_hostname(router->remote_addr);
    const char *port = lo_address_get_port(router->remote_addr);
    const char *host_to_match = lo_address_get_hostname(address);
    const char *port_to_match = lo_address_get_port(address);

    while (router) {
        if (strcmp(host, host_to_match)==0 && strcmp(port, port_to_match)==0)
            return router;
        router = router->next;
    }
    return 0;
}

mapper_router mapper_router_find_by_remote_name(mapper_router router,
                                                const char* dest_name)
{
    int n = strlen(dest_name);
    const char *slash = strchr(dest_name+1, '/');
    if (slash)
        n = slash - dest_name;

    while (router) {
        if (strncmp(router->remote_name, dest_name, n)==0)
            return router;
        router = router->next;
    }
    return 0;
}
