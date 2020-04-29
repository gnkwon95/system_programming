#define UNW_LOCAL_ONLY
#include <stdlib.h>
#include <stdio.h>
#include <libunwind.h>


int get_callinfo(char *fname, size_t fnlen, unsigned long long *ofs)
{

	unw_cursor_t cursor;
	unw_context_t context;

	unw_getcontext(&context);
	unw_init_local(&cursor, &context);

	unw_step(&cursor);
	unw_step(&cursor);
	unw_step(&cursor);

	unw_word_t offset;

	unw_get_proc_name(&cursor, fname, fnlen, &offset);
	*ofs = (unsigned long long) offset-0x5;	

	return 1;
}
