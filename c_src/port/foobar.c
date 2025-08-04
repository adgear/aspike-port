/* foobar.c */

#include "ei.h"
#include <string.h>
#include <stdlib.h>

typedef char byte;

int write_cmd(byte *buf, int len, int fd);
int ifail(int ind, int fd);

int foo(int x);
int bar(int y);

int call_foo(const char *buf, int *index, int arity, int fd_out);
int call_bar(const char *buf, int *index, int arity, int fd_out);

int call_foo(const char *buf, int *index, int arity, int fd_out) {
    long arg = 0;
    long res = 0;
    ei_x_buff res_buf;

    if (ei_decode_long(buf, index, &arg) != 0) {
        ifail(4, fd_out);
        return 0;
    }
    res = foo((int)arg);
    ei_x_new_with_version(&res_buf);
    ei_x_encode_tuple_header(&res_buf, 2);
    ei_x_encode_atom(&res_buf, "ok");
    ei_x_encode_long(&res_buf, res);
    write_cmd(res_buf.buff, res_buf.index, fd_out);
    ei_x_free(&res_buf);
    return 1;
}

int call_bar(const char *buf, int *index, int arity, int fd_out) {
    long arg = 0;
    long res = 0;
    ei_x_buff res_buf;

    if (ei_decode_long(buf, index, &arg) != 0) {
        ifail(4, fd_out);
        return 0;
    }
    res = bar((int)arg);
    ei_x_new_with_version(&res_buf);
    ei_x_encode_tuple_header(&res_buf, 2);
    ei_x_encode_atom(&res_buf, "ok");
    ei_x_encode_long(&res_buf, res);
    write_cmd(res_buf.buff, res_buf.index, fd_out);
    ei_x_free(&res_buf);
    return 1;
}
