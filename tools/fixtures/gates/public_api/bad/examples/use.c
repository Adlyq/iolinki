#include "iolinki/demo.h"

int fixture_use(void)
{
    return iolink_demo_live(1) + iolink_demo_undocumented(2) + iolink_demo_inline(3);
}
