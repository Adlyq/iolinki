#ifndef FIXTURE_DEMO_H
#define FIXTURE_DEMO_H

/** @brief Increment a value. */
static inline int iolink_demo_inline(int value)
{
    return value + 1;
}

/** @brief Increment a value through the exported entry point. */
int iolink_demo_live(int value);

/** @brief Orphaned entry point with no caller. */
int iolink_demo_orphan(int value);

/** @brief Entry point missing from the generated table. */
int iolink_demo_undocumented(int value);

#endif /* FIXTURE_DEMO_H */
