/* ei.c */

#include "ei.h"
#include <string.h>
#include <stdlib.h>
#include <string>

typedef char byte;

int read_cmd(byte *buf, int fd);

int ifail(int ind, int fd);
int fail(const char *msg, int fd);
int note(const char *msg, int fd);
int is_function_call(const char *buf, int *index, int *arity);
int function_call(const char *buf, int *index, int arity, int fd_out);
void logfile(std::string str);

int main() {
    byte buf[1000];
    int index = 0;
    int version = 0;
    int arity = 0;

    ei_init();

    int fd_in = 3;
    int fd_out = 4;

    while (read_cmd(buf, fd_in) > 0) {
        index = 0;
        if (ei_decode_version(buf, &index, &version) != 0)  {
            ifail(0, fd_out);
            exit(1);
        }
        if (is_function_call(buf, &index, &arity) != 0) {
            function_call(buf, &index, arity, fd_out);
            continue;
        } else {
            fprintf(stderr, "not is_function_call: %s\n", buf + index);
            note(buf + index, fd_out);
            continue;
        }
    }
}
