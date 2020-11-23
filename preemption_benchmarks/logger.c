#include "logger.h"

#if LOGGER_ENABLE

mlog_data_t g_md;
__thread int t_rank = -1;

#endif
