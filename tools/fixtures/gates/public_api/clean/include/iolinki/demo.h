#ifndef FIXTURE_DEMO_H
#define FIXTURE_DEMO_H

/** @brief Increment a value. */
static inline int iolink_demo_inline(int value)
{
    return value + 1;
}

/** @brief Increment a value through the exported entry point. */
int iolink_demo_live(int value);

#endif /* FIXTURE_DEMO_H */
