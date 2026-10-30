//variables.h
#include <limits.h>
const char *fifo1 = "fss_in"; //fss_console writes -> fss_manager reads
const char *fifo2 = "fss_out"; //fss_manager writes -> fss_console reads
const int max__size = PATH_MAX * 3; //max size of the string