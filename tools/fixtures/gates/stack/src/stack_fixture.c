#if defined(__GNUC__)
#define FIXTURE_NOINLINE __attribute__((noinline))
#else
#define FIXTURE_NOINLINE
#endif

static FIXTURE_NOINLINE int level4(int value)
{
    volatile unsigned char buffer[96];
    unsigned index;
    for (index = 0U; index < sizeof(buffer); index++) {
        buffer[index] = (unsigned char) (value + (int) index);
    }
    return value + (int) buffer[(unsigned) value & 63U];
}

static FIXTURE_NOINLINE int level3(int value)
{
    volatile unsigned char buffer[80];
    unsigned index;
    for (index = 0U; index < sizeof(buffer); index++) {
        buffer[index] = (unsigned char) (value + (int) index);
    }
    return level4(value + 1) + (int) buffer[(unsigned) value & 63U];
}

static FIXTURE_NOINLINE int level2(int value)
{
    volatile unsigned char buffer[64];
    unsigned index;
    for (index = 0U; index < sizeof(buffer); index++) {
        buffer[index] = (unsigned char) (value + (int) index);
    }
    return level3(value + 1) + (int) buffer[(unsigned) value & 63U];
}

static FIXTURE_NOINLINE int level1(int value)
{
    return level2(value + 1);
}

static FIXTURE_NOINLINE int recursion_a(int value);
static FIXTURE_NOINLINE int recursion_b(int value);

static FIXTURE_NOINLINE int recursion_b(int value)
{
    return (value <= 0) ? 0 : recursion_a(value - 1) + (value & 7);
}

static FIXTURE_NOINLINE int recursion_a(int value)
{
    return (value <= 0) ? 0 : recursion_b(value - 1) + (value & 7);
}

int iolink_device_process(int value)
{
    return level1(value);
}

int iolink_dll_process_isr(int value)
{
    return level1(value + 2);
}

int iolink_device_init(int value)
{
    return recursion_a(value);
}
