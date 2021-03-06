/* $Id$ */
/* Copyright (c) Slash'EM Development Team 2001-2003 */
/* NetHack may be freely redistributed.  See license for details. */

#ifndef NHXDR_H
#define NHXDR_H

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef MIN
#define MIN(a, b)	((a) <= (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)	((a) >= (b) ? (a) : (b))
#endif

/*
 * Assume an ANSI X3.159 compatible compiler
 */
#define char			int
#define short			int
#define boolean		int
#endif

#define NHEXT_XDR_RNDUP(x)	(((x) - 1 | 3) + 1)

typedef enum { nhext_xdr_false = 0, nhext_xdr_true = 1 } nhext_xdr_bool_t;

enum nhext_xdr_op {
    NHEXT_XDR_ENCODE, NHEXT_XDR_DECODE, NHEXT_XDR_FREE, NHEXT_XDR_COUNT
};

typedef struct nhext_xdr {
    void *x_data;
    enum nhext_xdr_op x_op;
    nhext_xdr_bool_t (*x_write)(struct nhext_xdr *xdrs, const void *addr,
      int size);
    nhext_xdr_bool_t (*x_read)(struct nhext_xdr *xdrs, void *addr, int size);
    void (*x_destroy)(struct nhext_xdr *xdrs);
    unsigned int x_pos, x_size;
} NhExtXdr;

typedef struct NhExtIO_ NhExtIO;
typedef int (*nhext_io_func)(void *handle, void *buf, unsigned int len);
typedef struct NhExtNB_ NhExtNB;

extern unsigned int nhext_xdr_getpos(NhExtXdr *);
extern nhext_xdr_bool_t nhext_xdr_setpos(NhExtXdr *, unsigned int);
extern void nhext_xdr_free(nhext_xdr_bool_t (*)(), char *);
extern void nhext_xdrmem_create(NhExtXdr *, char *, unsigned int,
			enum nhext_xdr_op);
extern void nhext_xdrio_create(NhExtXdr *, NhExtIO *, enum nhext_xdr_op);

#define nhext_xdr_destroy(xdrs)	(*(xdrs)->x_destroy)(xdrs)

extern nhext_xdr_bool_t nhext_xdr_long(NhExtXdr *, long *);
extern nhext_xdr_bool_t nhext_xdr_u_long(NhExtXdr *, unsigned long *);
extern nhext_xdr_bool_t nhext_xdr_short(NhExtXdr *, short *);
extern nhext_xdr_bool_t nhext_xdr_u_short(NhExtXdr *, unsigned short *);
extern nhext_xdr_bool_t nhext_xdr_int(NhExtXdr *, int *);
extern nhext_xdr_bool_t nhext_xdr_u_int(NhExtXdr *, unsigned int *);
extern nhext_xdr_bool_t nhext_xdr_char(NhExtXdr *, char *);
extern nhext_xdr_bool_t nhext_xdr_u_char(NhExtXdr *, unsigned char *);
extern nhext_xdr_bool_t nhext_xdr_enum(NhExtXdr *, int *);
extern nhext_xdr_bool_t nhext_xdr_bool(NhExtXdr *, nhext_xdr_bool_t *);
extern nhext_xdr_bool_t nhext_xdr_string(NhExtXdr *, char **, unsigned int);
extern nhext_xdr_bool_t nhext_xdr_bytes(NhExtXdr *, char **, unsigned int *,
  unsigned int);
extern nhext_xdr_bool_t nhext_xdr_wrapstring(NhExtXdr *, char **);
extern nhext_xdr_bool_t nhext_xdr_vector(NhExtXdr *, char *, unsigned int,
  unsigned int, nhext_xdr_bool_t (*)());
extern nhext_xdr_bool_t nhext_xdr_array(NhExtXdr *, char **, unsigned int *,
  unsigned int, unsigned int, nhext_xdr_bool_t (*)());

#define NHEXT_IO_RDONLY		1
#define NHEXT_IO_WRONLY		2
#define NHEXT_IO_NBLOCK		4
#define NHEXT_IO_NOAUTOFILL	8
#define NHEXT_IO_LINEBUF	16	/* Only honoured by text based calls */
#define NHEXT_IO_SIMPLEBUFFER	32
#define NHEXT_IO_PENDING	64

#if __GNUC__ >= 3 || __GNUC__ == 2 && __GNUC_MINOR__ >= 5
#define NHEXT__PRINTF(p_fmt, p_arg0) \
	__attribute__((format(printf, p_fmt, p_arg0)))
#else
#define NHEXT__PRINTF(p_fmt, p_arg0)
#endif

extern NhExtIO *nhext_io_open(nhext_io_func, void *, unsigned int);
extern int nhext_io_close(NhExtIO *);
extern unsigned int nhext_io_getmode(NhExtIO *);
extern void nhext_io_setmode(NhExtIO *, unsigned int);
extern void nhext_io_setautofill_limit(NhExtIO *, unsigned int);
extern void nhext_io_setnbfunc(NhExtIO *, nhext_io_func);
extern int nhext_io_filbuf(NhExtIO *, int);
extern int nhext_io_getc(NhExtIO *);
extern int nhext_io_read(NhExtIO *, char *, int);
extern char *nhext_io_getpacket(NhExtIO *, int *);
extern int nhext_io_willblock(NhExtIO *);
extern int nhext_io_flush(NhExtIO *);
extern int nhext_io_fputc(int, NhExtIO *);
extern int nhext_io_write(NhExtIO *, char *, int);
extern int nhext_io_writet(NhExtIO *, char *, int);
#ifdef _STDARG_H
extern int nhext_io_vprintf(NhExtIO *, char *, va_list);
#endif
extern int nhext_io_printf(NhExtIO *, char *, ...) NHEXT__PRINTF(2, 3);

extern NhExtNB *nhext_nb_open(nhext_io_func, void *);
extern int nhext_nb_close(NhExtNB *);
extern int nhext_nb_read(NhExtNB *, char *, int, int);

#endif /* NHXDR_H */
