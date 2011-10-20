#pragma once

// TODO: consider dropping some of these

#include <algorithm>

#include <string>
// TODO: try to get rid of this
using std::string; // hack for guid.h to work

#include <util/system/atomic.h>
#include <util/system/defaults.h>
#include <util/system/mutex.h>
#include <util/system/event.h>
#include <util/system/thread.h>
#include <util/system/file.h>
#include <util/system/hostname.h>
#include <util/system/yield.h>
#include <util/system/atexit.h>
#include <util/system/spinlock.h>

#include <util/charset/wide.h>

#include <util/thread/lfqueue.h>

#include <util/memory/tempbuf.h>

#include <util/generic/list.h>
#include <util/generic/deque.h>
#include <util/generic/utility.h>
#include <util/generic/stroka.h>
#include <util/generic/ptr.h>
#include <util/generic/vector.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/singleton.h>
#include <util/generic/typehelpers.h>
#include <util/generic/yexception.h>
#include <util/generic/pair.h>
#include <util/generic/algorithm.h>

#include <util/datetime/base.h>
#include <util/datetime/cputimer.h>

#include <util/string/printf.h>
#include <util/string/cast.h>

#include <util/random/random.h>

#include <util/stream/str.h>
#include <util/stream/input.h>
#include <util/stream/output.h>
#include <util/stream/file.h>

#include <util/folder/filelist.h>
#include <util/folder/dirut.h>

#include <util/config/last_getopt.h>

#include <util/server/http.h>
#include <util/autoarray.h>
#include <util/ysaveload.h>
#include <util/str_stl.h>

#include "intrusive_ptr.h"
#include "ptr.h"

// This define enables tracking of reference-counted objects to provide
// various insightful information on memory usage and object creation patterns.
#define ENABLE_REF_COUNTED_TRACKING

// This define enables thread affinity check -- a user-defined verification ensuring
// that some functions are called from particular threads.
#define ENABLE_THREAD_AFFINITY_CHECK

#ifdef _MSC_VER
    // C4505: unreferenced local function has been removed
    #pragma warning (disable : 4505)
    // C4121: alignment of a member was sensitive to packing
    #pragma warning (disable : 4121)
    // C4503: decorated name length exceeded, name was truncated
    #pragma warning (disable : 4503)
    // C4714: function marked as __forceinline not inlined
    #pragma warning (disable: 4714)
    // C4250: inherits via dominance
    #pragma warning (disable: 4250)
#endif

