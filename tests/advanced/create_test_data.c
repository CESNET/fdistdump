
#include <libnf.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define UNIQUE_FLOWS_CNT        17

#define INITIAL_TIMESTAMP       1433222111000

#define DEFAULT_FILENAME "test.nfcap"


enum protocols {
        P_ICMP = 1,
        P_TCP = 6,
        P_UDP = 17
};

const char *addresses[] = {
        "10.10.11.11",
        "10.123.234.1",
        "10.11.22.33",
        "10.245.11.241",
        "172.27.194.29", // 5th (index 4)
        "172.31.2.1",
        "172.2.1.199",
        "192.168.1.1",
        "192.168.0.1",
        "192.168.123.124", // 10th (index 9)

        "fd69:abdc:93bc:d96c::1",
        "fd52:4efb:6b9d:c7d7::2",
        "fd13:4efb:6b9d:c7d7::20",
        "fd83:4efb:6b9d:c7d7::4",
        "fd3b:6ef4:84ed:26b5::3"
};

const uint16_t ports [] = {
        0,
        20,
        23,
        53,
        80,
        443,
        666
};

enum uf_fields {
        UF_SRCIP = 0,
        UF_DSTIP,
        UF_PORT,
        UF_PROTO,
        UF_BYTES,
        UF_PKTS,
        UF_FLOWS,
        UF_TYPE,
};

/// B bytes in P packets and F flows was transfered among given addresses.
/// Given port is port bonded to source IP.
const int unique_flows [UNIQUE_FLOWS_CNT] [8] = {
        // {srcIP, dstIP, Port, Proto, Bytes, Packets, Flows, Type}
        // v4
        {1, 2, 2, P_TCP, 18000, 100, 4, AF_INET},
        {3, 9, 3, P_TCP, 24, 1, 1, AF_INET},
        {3, 2, 4, P_TCP, 190000, 2000, 10, AF_INET},
        {6, 1, 2, P_TCP, 18000, 50, 5, AF_INET},
        {7, 8, 1, P_TCP, 123, 2, 1, AF_INET},
        {2, 9, 6, P_TCP, 111222333, 111222, 200, AF_INET},
        {0, 3, 1, P_UDP, 1000, 2, 2,     AF_INET},
        {5, 7, 2, P_UDP, 300400, 120, 120, AF_INET},
        {1, 2, 5, P_TCP, 22222, 333, 6, AF_INET},
        {1, 2, 5, P_UDP, 123456, 200, 190, AF_INET},
        {5, 7, 0, P_ICMP, 30, 1, 1, AF_INET},
        {5, 7, 0, P_ICMP, 30, 1, 1, AF_INET},
        {5, 7, 0, P_ICMP, 30, 1, 1, AF_INET},
        {5, 7, 0, P_ICMP, 30, 1, 1, AF_INET},
        //v6
        {10, 12, 1, P_TCP, 666, 1, 1, AF_INET6},
        {14, 11, 6, P_TCP, 101010, 1010, 10, AF_INET6},
        {11, 13, 2, P_UDP, 900000, 1000, 990, AF_INET6},
        // total: B: 112 897 264, P: 116042
};

void print_bits (uint32_t data)
{
        for (int i = 31; i >= 0; --i){
                if((data >> i) & 0x1){
                    printf("1");
                } else {
                    printf("0");
                }
            if (i % 8 == 0){
                printf(" ");
            }
        }
}

void print_addr_bits (lnf_ip_t addr)
{
        print_bits(addr.data[0]);
        printf(" | ");
        print_bits(addr.data[1]);
        printf(" | ");
        print_bits(addr.data[2]);
        printf(" | ");
        print_bits(addr.data[3]);
        printf("\n");
}

int main (int argc, char **argv)
{

        lnf_file_t *filep;
        lnf_rec_t *recp;
        lnf_brec1_t brec;
        uint32_t input, output;

        int compress = 1;
//        int aggip = 1;
        int rec_cnt = 0;

        char *filename = DEFAULT_FILENAME;

        /// Getting filename >>>
        if (argc > 1) {
                if (argc == 2){
                        filename = argv[1];
                } else {
                    fprintf(stderr, "Unexpected program arguments \
                            (expecting optional filename only).\n");
                    exit(1);
                }
        }

        if (strlen(filename) < 1){
                fprintf(stderr, "Wrong output filename with size %d.\n",
                        (int) strlen(filename));
                exit(1);
        }
        /// <<<

        /// Try to open output file >>>
        /* open lnf file descriptor */
        if (lnf_open(&filep, filename, LNF_WRITE | ( compress ? LNF_COMP : 0 ),
                     "myfile") != LNF_OK) {
                fprintf(stderr, "Can not open file \"%s\".\n", filename);
                exit(1);
        }
        /// <<<

        srand(time(NULL));

        /* initialise empty record */
        lnf_rec_init(&recp);

        for (int i = 0; i < UNIQUE_FLOWS_CNT; i++) {
                int f_cnt = unique_flows[i][UF_FLOWS];
                // There is at least 20B and 1pkt for every flow:
                int bytes = unique_flows[i][UF_BYTES] - f_cnt * 20;
                int packets = unique_flows[i][UF_PKTS] - f_cnt;

                uint16_t rand_ports [f_cnt];
                int rand_volume [f_cnt][2];

                int b_sum = 0;
                int p_sum = 0;

                // Generate ports and volume
                for (int j = 0; j < f_cnt; ++j){
                        rand_ports[j] = (uint16_t) rand() % 60000 + 5000;

                        // remove duplicity
                        bool dup = true;
                        while (dup){
                                dup = false;
                                for (int k = 0; k < j; ++k){
                                        if (rand_ports[j] == rand_ports[k]){
                                                dup = true;
                                                rand_ports[j] =
                                                        (rand_ports[j] + 1)
                                                        % 65000;
                                                break;
                                        }
                                }
                        }

                        if (j < f_cnt - 1){
                            int p = 0;
                            int b = 0;
                            int b_min, b_max;

                            if (packets){
                                    p = rand() % packets;
                            }

                            b_min = bytes - (20 * p);
                            b_max = (p + 1) * 1500;
                            if (b_max > bytes){
                                    b_max = bytes;
                            }

                            if (b_min > 0){
                                    b = (rand() % b_max) + (20 * p);
                            }

                            if (packets - p >= 0 && bytes - b >= 0){
                                    rand_volume[j][0] = 20 + b;
                                    rand_volume[j][1] = 1 + p;
//                                    printf("     %d - %d (%d/%d) = ", bytes, b, rand_volume[j][0], j);
                                    bytes -= b;
                                    packets -= p;
                            } else {
                                    rand_volume[j][0] = 20 + bytes;
                                    rand_volume[j][1] = 1 + packets;
                                    bytes = 0;
                                    packets = 0;
                            }
                        } else {
                            rand_volume[j][0] = 20 + bytes;
                            rand_volume[j][1] = 1 + packets;
                        }

                        b_sum += rand_volume[j][0];
                        p_sum += rand_volume[j][1];

                        /* prepare data in asic record1 (lnf_brec1_t) */
                        brec.first = INITIAL_TIMESTAMP + i * 50000 + j * 1000;
                        brec.last =  brec.first + rand() % 300000 + 10;
                        memset(brec.dstaddr.data, 0,
                               sizeof(brec.dstaddr.data[0]) * 4);
                        memset(brec.srcaddr.data, 0,
                               sizeof(brec.srcaddr.data[0]) * 4);

//                        if (j % 2){
                        if (unique_flows[i][UF_TYPE] == AF_INET){
                            inet_pton(unique_flows[i][UF_TYPE],
                                      addresses[unique_flows[i][UF_SRCIP]],
                                      &brec.srcaddr.data[3]);
                            inet_pton(unique_flows[i][UF_TYPE],
                                      addresses[unique_flows[i][UF_DSTIP]],
                                      &brec.dstaddr.data[3]);
                        } else {
                            inet_pton(unique_flows[i][UF_TYPE],
                                      addresses[unique_flows[i][UF_SRCIP]],
                                      &brec.srcaddr);
                            inet_pton(unique_flows[i][UF_TYPE],
                                      addresses[unique_flows[i][UF_DSTIP]],
                                      &brec.dstaddr);
                        }
                        brec.srcport = ports[unique_flows[i][UF_PORT]];
                        brec.dstport = rand_ports[j];
//                        } else {
//                                inet_pton(unique_flows[i][UF_TYPE],
//                                          addresses[unique_flows[i][UF_SRCIP]],
//                                          &brec.dstaddr.data[3]);
//                                inet_pton(unique_flows[i][UF_TYPE],
//                                          addresses[unique_flows[i][UF_DSTIP]],
//                                          &brec.srcaddr.data[3]);
//                                brec.dstport = ports[unique_flows[i][UF_PORT]];
//                                brec.srcport = rand_ports[j];
//                        }
                        brec.prot = unique_flows[i][UF_PROTO];
                        brec.bytes = rand_volume[j][0];
                        brec.pkts = rand_volume[j][1];
                        brec.flows = 1;

                        if (brec.prot == P_ICMP){
                                brec.dstport = 0;
                                brec.srcport = 0;
                        }

                        input = i % 5; /* make input index interface 0 - 5 */
                        output = i % 10; /* make output index interface 0 - 5 */

//                        if (aggip) {
//                                brec.bytes = i;
//                                brec.srcaddr.data[1] = 1000 + (i % aggip);
//                        }
                        /* prepare record */
                        lnf_rec_fset(recp, LNF_FLD_BREC1, &brec);

                        /* set input and output interface */
                        lnf_rec_fset(recp, LNF_FLD_INPUT, &input);
                        lnf_rec_fset(recp, LNF_FLD_OUTPUT, &output);

                        rec_cnt++;
                        /* write record to file */
                        if (lnf_write(filep, recp) != LNF_OK) {
                                fprintf(stderr, "Can not write record no %d\n", i);
                        }
                }
//                printf("------------------------------------------\n");
//
//                printf("O. %s:%u -> %s:%u, %i, %i, %i, %i\n",
//                                addresses[unique_flows[i][UF_SRCIP]];,
//                                ports[unique_flows[i][UF_PORT]],
//                                addresses[unique_flows[i][UF_DSTIP]];,
//                                0,
//                                unique_flows[i][UF_PROTO],
//                                unique_flows[i][UF_BYTES],
//                                unique_flows[i][UF_PKTS],
//                                unique_flows[i][UF_FLOWS]);
//                printf("C. %s:%u -> %s:%u, %i, %i, %i, %i\n",
//                                addresses[unique_flows[i][UF_SRCIP]];,
//                                ports[unique_flows[i][UF_PORT]],
//                                addresses[unique_flows[i][UF_DSTIP]];,
//                                0,
//                                unique_flows[i][UF_PROTO],
//                                b_sum,
//                                p_sum,
//                                unique_flows[i][UF_FLOWS]);
//                printf("##########################################\n");
        }

        /* return memory */
        lnf_rec_free(recp);
        lnf_close(filep);

        printf("%d records was written to %s\n", rec_cnt, filename);
//        printf("You can read it via cmd 'nfdump -r %s -o raw'\n", filename);

        return 0;
}

