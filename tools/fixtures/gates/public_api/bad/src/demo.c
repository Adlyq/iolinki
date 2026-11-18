#include "iolinki/demo.h"

int iolink_demo_live(int value)
{
    return value + 1;
}

int iolink_demo_orphan(int value)
{
    return value + 1;
}

int iolink_demo_undocumented(int value)
{
    return iolink_demo_inline(value);
}
