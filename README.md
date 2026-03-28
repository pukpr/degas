read the degas.pdf document for context.
The idea is that the Ada tasking runtime calls a few pthread routines to schedule and synchronize.
These are intercepted by routines in degas.c via the runtime LD_PRELOAD of degas.so.
The issue is that degas.c was written for an old version of glibc, which no longer uses spinlocks but futexes.
Can use degas.c as a guide but not a final solution because it may not be easily shoe-horned in due to changes in pthread.h.
There are not many interactions inside dega.c as the replacement runtime treats time as simulated time so immediate scheduling of events occurs.



