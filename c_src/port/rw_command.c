/* rw_command.c */

#include <stdio.h>
#include <unistd.h>
#include <string>

#define bytes_to_u16(MSB,LSB) (((unsigned int) ((unsigned char) MSB)) & 255)<<8 | (((unsigned char) LSB)&255)

typedef char byte;

int read_exact(byte *buf, unsigned int len, int fd);
int write_exact(byte *buf, int len, int fd);
int read_cmd(byte *buf, int fd);
int write_cmd(byte *buf, int len, int fd);
void logfile(std::string str);

int read_exact(byte *buf, unsigned int len, int fd)
{
  int i; 
  unsigned int got=0;

  do {
      if ((i = read(fd, buf+got, len-got)) <= 0){
          return(i);
      }
    got += i;
  } while (got<len);

  return(len);
}

int write_exact(byte *buf, int len, int fd)
{
  int i, wrote = 0;

  do {
    if ((i = write(fd, buf+wrote, len-wrote)) <= 0)
      return (i);
    wrote += i;
  } while (wrote<len);

  return (len);
}

int read_cmd(byte *buf, int fd)
{
  unsigned int len;

  if (read_exact(buf, 2, fd) != 2)
    return(-1);

  len = bytes_to_u16(buf[0],buf[1]);
  //len = (buf[0] << 8) | buf[1];
  return read_exact(buf, len, fd);
}

int write_cmd(byte *buf, int len, int fd)
{
  byte li;

  li = (len >> 8) & 0xff;
  write_exact(&li, 1, fd);
  
  li = len & 0xff;
  write_exact(&li, 1, fd);

  return write_exact(buf, len, fd);
}
