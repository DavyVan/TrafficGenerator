#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <pthread.h>

#include "../common/common.h"

#ifndef max
    #define max(a,b) ((a) > (b) ? (a) : (b))
#endif

char config_file_name[80] = {'\0'}; //configuration file name
char dist_file_name[80] = {'\0'};   //size distribution file name
char fct_log_name[] = "flows.txt";
char rct_log_name[] = "reqs.txt";
int seed = 0; //random seed

/* parameters of servers */
int num_server = 0; //total number of servers
int *server_port = NULL;   //ports of servers
char (*server_addr)[20] = NULL;    //IP addresses of servers

int num_fanout = 0;    //Number of fanouts
int *fanout_size = NULL;
int *fanout_prob = NULL;
int fanout_prob_total = 0;

int num_service = 0; //Number of services
int *service_dscp = NULL;
int *service_prob = NULL;
int service_prob_total = 0;

int num_rate = 0; //Number of sending rates
int *rate_value = NULL;
int *rate_prob = NULL;
int rate_prob_total = 0;

double load = 0; //Network load (Mbps)
int req_total_num = 0; //Total number of requests


/* Print usage of the program */
void print_usage(char *program);
/* Read command line arguments */
void read_args(int argc, char *argv[]);
/* Read configuration file */
void read_config(char *file_name);
/* Clean up resources */
void cleanup();

int main(int argc, char *argv[])
{
    struct timeval time;    //record current system time

    read_args(argc, argv);

    if (seed == 0)
    {
        gettimeofday(&time, NULL);
        srand((time.tv_sec*1000000) + time.tv_usec);
    }
    else
        srand(seed);

    read_config(config_file_name);

    cleanup();

    return 0;
}

/* Print usage of the program */
void print_usage(char *program)
{
    printf("Usage: %s [options]\n", program);
    printf("-c <file>    configuration file name (required)\n");
    printf("-s <seed>    random seed value (default current time)\n");
    printf("-h           display help information\n");
}

/* Read command line arguments */
void read_args(int argc, char *argv[])
{
    int i = 1;

    if (argc == 1)
    {
        print_usage(argv[0]);
        exit(EXIT_SUCCESS);
    }

    while (i < argc)
    {
        if (strlen(argv[i]) == 2 && strcmp(argv[i], "-c") == 0)
        {
            if (i+1 < argc && strlen(argv[i+1]) < sizeof(config_file_name))
            {
                sprintf(config_file_name, "%s", argv[i+1]);
                i += 2;
            }
            /* cannot read IP address */
            else
            {
                printf("Cannot read configuration file name\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-s") == 0)
        {
            if (i+1 < argc)
            {
                seed = atoi(argv[i+1]);
                i += 2;
            }
            /* cannot read port number */
            else
            {
                printf("Cannot read seed value\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        }
        else
        {
            printf("Invalid option %s\n", argv[i]);
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }
}

/* Read configuration file */
void read_config(char *file_name)
{
    FILE *fd=NULL;
    char line[256]={'\0'};
    char key[80]={'\0'};
    num_server = 0;    //Number of senders
    int num_load = 0;   //Number of network loads
    int num_req = 0;    //Number of requests
    int num_dist = 0;   //Number of flow size distributions
    num_fanout = 0;    //Number of fanouts (optional)
    num_service = 0; //Number of services (optional)
    num_rate = 0; //Number of sending rates (optional)

    printf("Reading configuration file %s\n", file_name);

    /* Parse configuration file for the first time */
    fd = fopen(file_name, "r");
    if (!fd)
        error("Error: fopen");

    while (fgets(line, sizeof(line), fd) != NULL)
    {
        sscanf(line, "%s", key);
        if (!strcmp(key, "server"))
            num_server++;
        else if (!strcmp(key, "load"))
            num_load++;
        else if (!strcmp(key, "num_reqs"))
            num_req++;
        else if (!strcmp(key, "req_size_dist"))
            num_dist++;
        else if (!strcmp(key, "fanout"))
            num_fanout++;
        else if (!strcmp(key, "service"))
            num_service++;
        else if (!strcmp(key, "rate"))
            num_rate++;
        else
            error("Error: invalid key in configuration file");
    }

    fclose(fd);

    if (num_server < 1)
        error("Error: configuration file should provide at least one server");
    if (num_load != 1)
        error("Error: configuration file should provide one network load");
    if (num_req != 1)
        error("Error: configuration file should provide one total number of requests");
    if (num_dist != 1)
        error("Error: configuration file should provide one request size distribution");

    /* Initialize configuration */
    /* server IP addresses and ports */
    server_port = (int*)malloc(num_server);
    server_addr = (char (*)[20])malloc(num_server * sizeof(char[20]));
    /* fanout size and probability */
    fanout_size = (int*)malloc(max(num_fanout, 1) * sizeof(int));
    fanout_prob = (int*)malloc(max(num_fanout, 1) * sizeof(int));
    /* service DSCP and probability */
    service_dscp = (int*)malloc(max(num_service, 1) * sizeof(int));
    service_prob = (int*)malloc(max(num_service, 1) * sizeof(int));
    /* sending rate value and probability */
    rate_value = (int*)malloc(max(num_rate, 1) * sizeof(int));
    rate_prob = (int*)malloc(max(num_rate, 1) * sizeof(int));

    if (!server_port || !server_addr || !fanout_size || !fanout_prob || !service_dscp || !service_prob || !rate_value || !rate_prob)
    {
        cleanup();
        error("Error: malloc");
    }

    /* Second time */
    num_server = 0;
    num_fanout = 0;
    num_service = 0;
    num_rate = 0;

    fd = fopen(file_name, "r");
    if (!fd)
    {
        error("Error: fopen");
        cleanup();
    }

    while (fgets(line, sizeof(line), fd) != NULL)
    {
        remove_newline(line);
        sscanf(line, "%s", key);

        if (!strcmp(key, "server"))
        {
            sscanf(line, "%s %s %d", key, server_addr[num_server], &server_port[num_server]);
            printf("Server[%d]: %s, Port: %d\n", num_server, server_addr[num_server], server_port[num_server]);
            num_server++;
        }
        else if (!strcmp(key, "load"))
        {
            sscanf(line, "%s %lfMbps", key, &load);
            printf("Network Load: %.2f Mbps\n", load);
        }
        else if (!strcmp(key, "num_reqs"))
        {
            sscanf(line, "%s %d", key, &req_total_num);
            printf("Number of Requests: %d\n", req_total_num);
        }
        else if (!strcmp(key, "req_size_dist"))
        {
            sscanf(line, "%s %s", key, dist_file_name);
            printf("Loading request size distribution: %s\n", dist_file_name);
        }
        else if (!strcmp(key, "fanout"))
        {
            sscanf(line, "%s %d %d", key, &fanout_size[num_fanout], &fanout_prob[num_fanout]);
            fanout_prob_total += fanout_prob[num_fanout];
            printf("Fanout: %d, Prob: %d\n", fanout_size[num_fanout], fanout_prob[num_fanout]);
            num_fanout++;
        }
        else if (!strcmp(key, "service"))
        {
            sscanf(line, "%s %d %d", key, &service_dscp[num_service], &service_prob[num_service]);
            service_prob_total += service_prob[num_service];
            printf("Service DSCP: %d, Prob: %d\n", service_dscp[num_service], service_prob[num_service]);
            num_service++;
        }
        else if (!strcmp(key, "rate"))
        {
            sscanf(line, "%s %dMbps %d", key, &rate_value[num_rate], &rate_prob[num_rate]);
            rate_prob_total += rate_prob[num_rate];
            printf("Rate: %dMbps, Prob: %d\n", rate_value[num_rate], rate_prob[num_rate]);
            num_rate++;
        }
    }

    fclose(fd);

    /* By default, fanout size is always 1 */
    if (num_fanout == 0)
    {
        num_fanout = 1;
        fanout_size[0] = 1;
        fanout_prob[0] = 100;
        fanout_prob_total = fanout_prob[0];
        printf("Fanout: %d, Prob: %d\n", fanout_size[0], fanout_prob[0]);
    }

    /* By default, DSCP value is 0 */
    if (num_service == 0)
    {
        num_service = 1;
        service_dscp[0] = 0;
        service_prob[0] = 100;
        service_prob_total = service_prob[0];
        printf("Service DSCP: %d, Prob: %d\n", service_dscp[0], service_prob[0]);
    }

    /* By default, no rate limiting */
    if (num_rate == 0)
    {
        num_rate = 1;
        rate_value[0] = 0;
        rate_prob[0] = 100;
        rate_prob_total = rate_prob[0];
        printf("Rate: %dMbps, Prob: %d\n", rate_value[0], rate_prob[0]);
    }
}

/* Clean up resources */
void cleanup()
{
    free(server_port);
    free(server_addr);

    free(fanout_size);
    free(fanout_prob);

    free(service_dscp);
    free(service_prob);

    free(rate_value);
    free(rate_prob);
}
