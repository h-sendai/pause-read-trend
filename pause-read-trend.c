#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "my_signal.h"
#include "my_socket.h"
#include "set_timer.h"
#include "set_cpu.h"
#include "get_num.h"
#include "logUtil.h"
#include "flow_ctrl_pause.h"

volatile sig_atomic_t has_alarm = 0;
volatile sig_atomic_t has_int   = 0;

int debug = 0;

int usage(void)
{
    char msg[] = "Usage: ./read-trend [-c cpu_num] [-d] [-P] [-p port] [-q] [-q ...] [-r rcvbuf] [-b bufsize] [-i interval] [-o output_file] [-s sleep_usec] ip_address[:port] [-I pause_interval_sec] if_name pause_time\n"
                 "default: port 24, read bufsize 1024kB, interval 1 second\n"
                 "-I: pause_interval_sec (default 5 seconds)\n"
                 "suffix k for kilo, m for mega to speficy bufsize\n"
                 "If both -p port and ip_address:port are specified, ip_address:port port wins\n"
                 "Options\n"
                 "-c cpu_num: set cpu number to be run\n"
                 "-d debug\n"
                 "-P: print pid\n"
                 "-p port\n port number\n"
                 "-q: enable quickack after connect()\n"
                 "-q -q: enable quickack after connect() and before every read()\n"
                 "-r rcvbuf: set socket receive buffer size\n"
                 "-b bufsize: read() buffer size (default: 1024kB)\n"
                 "-i sec: print interval (seconds. allow decimal)\n"
                 "-o file: out data to file\n"
                 "-s sleep_usec: usleep(sleep_usec) in sigalrm routine\n";

    fprintf(stderr, "%s\n", msg);

    return 0;
}

void sig_alrm(int signo)
{
    has_alarm = 1;
    return;
}

void sig_int(int signo)
{
    has_int = 1;
    return;
}

int print_pid()
{
    pid_t pid = getpid();
    fprintf(stderr, "pid: %d\n", pid);

    return 0;
}

int main(int argc, char *argv[])
{
    int rcvbuf   = 0;
    int bufsize  = 1*1024*1024;
    int port     = 24;
    char *buf    = NULL;
    int cpu_num  = -1;
    int enable_quickack = 0;
    char *interval_sec_str = "1.0";
    char *output = "";
    int sleep_usec = 0;
    int use_shutdown = 0;
    int pause_interval_sec = 5;

    int c;
    while ( (c = getopt(argc, argv, "c:dhi:I:o:Pp:qr:s:b:S")) != -1) {
        switch (c) {
            case 'h':
                usage();
                exit(0);
                break;
            case 'c':
                cpu_num = get_num(optarg);
                break;
            case 'd':
                debug += 1;
                break;
            case 'i':
                interval_sec_str = optarg;
                break;
            case 'I':
                pause_interval_sec = strtol(optarg, NULL, 0);
                break;
            case 'o':
                output = optarg;
                break;
            case 'P':
                print_pid();
                break;
            case 'p':
                port = strtol(optarg, NULL, 0);
                break;
            case 'q':
                enable_quickack += 1;
                break;
            case 'r':
                rcvbuf = get_num(optarg);
                break;
            case 's':
                sleep_usec = strtol(optarg, NULL, 0);
                break;
            case 'b':
                bufsize = get_num(optarg);
                break;
            case 'S':
                use_shutdown = 1;
                break;
            default:
                break;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc != 3) {
        usage();
        exit(1);
    }

    char *remote_host_info = argv[0];
    char *tmp = strdup(remote_host_info);
    char *remote_host = strsep(&tmp, ":");
    if (tmp != NULL) {
        port = strtol(tmp, NULL, 0);
    }

    char *if_name = argv[1];
    int pause_time = get_num(argv[2]);
    /* allow to specify pause time as 64k */
    if (pause_time == 65536) {
        pause_time = 65535;
    }
    if (pause_time < 0 || pause_time > 65535) {
        fprintf(stderr, "pause_time out of range: %d (should be 0 - 65535)\n", pause_time);
        exit(1);
    }

    if (debug) {
        fprintf(stderr, "if_name: %s, pause_time: %d\n", if_name, pause_time);
    }

    if (cpu_num != -1) {
        if (set_cpu(cpu_num) < 0) {
            fprintf(stderr, "set_cpu: %d fail\n", cpu_num);
            exit(1);
        }
    }

    FILE *fp = NULL;
    if (strlen(output)) {
        fp = fopen(output, "w");
        if (fp == NULL) {
            err(1, "fopen for %s", output);
        }
    }

    my_signal(SIGALRM, sig_alrm);
    my_signal(SIGINT,  sig_int);
    my_signal(SIGTERM, sig_int);

    buf = malloc(bufsize);
    if (buf == NULL) {
        err(1, "malloc for buf");
    }
    memset(buf, 0, bufsize);

    int sockfd = tcp_socket();
    if (sockfd < 0) {
        errx(1, "tcp_socket()");
    }

    if (rcvbuf > 0) {
        if (set_so_rcvbuf(sockfd, rcvbuf) < 0) {
            errx(1,  "set_so_rcvbuf");
        }
    }
    
    int pause_socket = create_pause_socket();
    if (pause_socket < 0) {
        exit(1);
    }

    if (connect_tcp(sockfd, remote_host, port) < 0) {
        errx(1, "tcp_connect");
    }

    if (debug) {
        fprintf(stderr, "enable_quickack: %d\n", enable_quickack);
        int my_port = get_port_num(sockfd);
        fprintf(stderr, "my_port: %d\n", my_port);
    }
    if (enable_quickack) {
        set_so_quickack(sockfd);
    }

    long interval_read_bytes = 0;
    long total_bytes         = 0;
    long interval_read_count = 0;

    struct timeval interval;
    conv_str2timeval(interval_sec_str, &interval);
    set_timer(interval.tv_sec, interval.tv_usec, interval.tv_sec, interval.tv_usec);

    struct timeval start, prev;
    gettimeofday(&start, NULL);
    prev = start;

    int alarm_count = 0; /* counter for alarm receive count */
    for ( ; ; ) {
        if (has_alarm) {
            has_alarm = 0;
            alarm_count += 1;
            struct timeval now, elapse;
            gettimeofday(&now, NULL);
            timersub(&now, &start, &elapse);
            timersub(&now, &prev,  &interval);
            double interval_sec = interval.tv_sec + 0.000001*interval.tv_usec;
            double transfer_rate_MB_s = interval_read_bytes / interval_sec / 1024.0 / 1024.0;
            double transfer_rate_Gb_s = MiB2Gb(transfer_rate_MB_s);
            printf("%ld.%06ld %.3f MB/s %.3f Gbps %ld\n",
                elapse.tv_sec, elapse.tv_usec, 
                transfer_rate_MB_s,
                transfer_rate_Gb_s,
                interval_read_count);
            fflush(stdout);
            interval_read_bytes = 0;
            interval_read_count = 0;
            prev = now;

            if (alarm_count % pause_interval_sec == 0) {
                if (debug) {
                    fprintf(stderr, "# send_pause_packet\n");
                }
                send_pause_packet(pause_socket, if_name, pause_time);
            }
            if (sleep_usec > 0) {
                usleep(sleep_usec);
            }
        }
        if (has_int) {
            /* stop alarm */
            my_signal(SIGALRM, SIG_IGN);

            if (use_shutdown) {
                if (shutdown(sockfd, SHUT_WR) < 0) {
                    err(1, "shutdown");
                }
            }
            if (close(sockfd) < 0) {
                err(1, "close");
            }

            struct timeval now, elapse;
            gettimeofday(&now, NULL);
            timersub(&now, &start, &elapse);
            double run_time_sec = elapse.tv_sec + 0.000001*elapse.tv_usec;
            double transfer_rate_MB_s = total_bytes / run_time_sec / 1024.0 / 1024.0;
            double transfer_rate_Gb_s = MiB2Gb(transfer_rate_MB_s);
            fprintf(stderr, "run_sec: %.3f seconds total_bytes: %ld bytes transfer_rate: %.3f MB/s %.3f Gbps\n",
                run_time_sec, total_bytes, transfer_rate_MB_s, transfer_rate_Gb_s);
            exit(0);
        }

        if (enable_quickack > 1) {
            set_so_quickack(sockfd);
        }

        int n = read(sockfd, buf, bufsize);
        if (n < 0) {
            if (errno == EINTR) {
                if (debug) {
                    fprintf(stderr, "EINTR\n");
                }
                continue;
            }
            else {
                err(1, "read socket");
            }
        }
        interval_read_bytes += n;
        total_bytes         += n;
        interval_read_count ++;

        if (strlen(output)) {
            int m = fwrite(buf, 1, n, fp);
            if (m == 0) {
                if (ferror(fp)) {
                    err(1, "fwrite");
                }
                else {
                    fprintf(stderr, "unknown error for fwrite\n");
                    exit(0);
                }
            }
        }
    }

    return 0;
}
