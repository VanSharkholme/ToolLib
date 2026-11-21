#include "pal_gpio_host.h"
#include "test_runner.h"

#define OK(call) TEST_ASSERT_EQUAL_INT(PAL_GPIO_STATUS_OK, (call))

void setUp(void) { PAL_GPIO_Host_Reset(); }
void tearDown(void) {}

static void test_configuration(void)
{
    PalGpioId id = PAL_GPIO_HOST_OUTPUT;
    PalGpioConfig desired = {.mode = PAL_GPIO_OUTPUT, .alternate = 12,
        .output_type = PAL_GPIO_OPEN_DRAIN, .speed = PAL_GPIO_SPEED_HIGH, .pull = PAL_GPIO_PULL_UP};
    OK(PAL_GPIO_SetConfig(id, &desired));
    PalGpioConfig actual;
    OK(PAL_GPIO_GetMode(id, &actual.mode));
    TEST_ASSERT_EQUAL_INT(desired.mode, actual.mode);
    OK(PAL_GPIO_GetOutputType(id, &actual.output_type));
    TEST_ASSERT_EQUAL_INT(desired.output_type, actual.output_type);
    OK(PAL_GPIO_GetSpeed(id, &actual.speed));
    TEST_ASSERT_EQUAL_INT(desired.speed, actual.speed);
    OK(PAL_GPIO_GetPull(id, &actual.pull));
    TEST_ASSERT_EQUAL_INT(desired.pull, actual.pull);
    OK(PAL_GPIO_GetAlternate(id, &actual.alternate));
    TEST_ASSERT_EQUAL_INT(desired.alternate, actual.alternate);
    OK(PAL_GPIO_Write(id, true));
    OK(PAL_GPIO_SetPull(id, PAL_GPIO_PULL_DOWN));
    OK(PAL_GPIO_SetAlternate(id, 5));
    OK(PAL_GPIO_SetSpeed(id, PAL_GPIO_SPEED_VERY_HIGH));
    OK(PAL_GPIO_SetOutputType(id, PAL_GPIO_PUSH_PULL));
    OK(PAL_GPIO_GetConfig(id, &actual));
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_OUTPUT, actual.mode);
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_PUSH_PULL, actual.output_type);
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_SPEED_VERY_HIGH, actual.speed);
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_PULL_DOWN, actual.pull);
    TEST_ASSERT_EQUAL_UINT(5, actual.alternate);
    bool level;
    OK(PAL_GPIO_SetMode(id, PAL_GPIO_ALTERNATE));
    OK(PAL_GPIO_SetConfig(id, &desired));
    OK(PAL_GPIO_Read(id, &level));
    TEST_ASSERT_TRUE(level);
    OK(PAL_GPIO_Host_SetLevel(id, false));
    OK(PAL_GPIO_Toggle(id));
    OK(PAL_GPIO_Read(id, &level));
    TEST_ASSERT_FALSE(level);
}

static void test_validation(void)
{
    PalGpioId id = PAL_GPIO_HOST_INPUT;
    PalGpioConfig cfg = {.alternate = 16};
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_STATUS_INVALID_ID, PAL_GPIO_SetConfig(PAL_GPIO_HOST_COUNT, &cfg));
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_STATUS_INVALID_ARGUMENT, PAL_GPIO_SetConfig(id, &cfg));
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_STATUS_INVALID_ARGUMENT, PAL_GPIO_GetConfig(id, NULL));
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_STATUS_INVALID_ARGUMENT, PAL_GPIO_SetInterrupt(id, true, PAL_GPIO_EDGE_NONE));
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_STATUS_INVALID_STATE, PAL_GPIO_Write(id, true));
    OK(PAL_GPIO_SetMode(id, PAL_GPIO_ALTERNATE));
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_STATUS_INVALID_STATE, PAL_GPIO_SetInterrupt(id, true, PAL_GPIO_EDGE_BOTH));
}

static void count_irq(PalGpioId id, void *context)
{
    TEST_ASSERT_EQUAL_UINT(PAL_GPIO_HOST_INPUT, id);
    ++*(unsigned *)context;
}

static void test_edges(void)
{
    unsigned count = 0;
    PalGpioId id = PAL_GPIO_HOST_INPUT;
    OK(PAL_GPIO_SetCallback(id, count_irq, &count));
    OK(PAL_GPIO_SetInterrupt(id, true, PAL_GPIO_EDGE_FALLING));
    OK(PAL_GPIO_Host_SetLevel(id, true));
    TEST_ASSERT_EQUAL_UINT(0, count);
    OK(PAL_GPIO_Host_SetLevel(id, false));
    OK(PAL_GPIO_SetInterrupt(id, true, PAL_GPIO_EDGE_BOTH));
    OK(PAL_GPIO_Host_SetLevel(id, true));
    OK(PAL_GPIO_Host_SetLevel(id, true));
    TEST_ASSERT_EQUAL_UINT(2, count);
    OK(PAL_GPIO_SetCallback(id, NULL, NULL));
    OK(PAL_GPIO_Host_SetLevel(id, false));
    TEST_ASSERT_EQUAL_UINT(2, count);
}

static void test_shared_line(void)
{
    PalGpioId a = PAL_GPIO_HOST_AF_INPUT, b = PAL_GPIO_HOST_SHARED_LINE;
    PalGpioConfig cfg = {.interrupt_enabled = true, .edge = PAL_GPIO_EDGE_RISING, .pull = PAL_GPIO_PULL_UP};
    OK(PAL_GPIO_SetConfig(a, &cfg));
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_STATUS_BUSY, PAL_GPIO_SetConfig(b, &cfg));
    OK(PAL_GPIO_GetConfig(b, &cfg));
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_NO_PULL, cfg.pull);
    TEST_ASSERT_FALSE(cfg.interrupt_enabled);
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_EDGE_NONE, cfg.edge);
    OK(PAL_GPIO_SetConfig(b, &cfg));
    OK(PAL_GPIO_SetInterrupt(b, false, PAL_GPIO_EDGE_NONE));
    OK(PAL_GPIO_GetInterrupt(a, &cfg.interrupt_enabled, &cfg.edge));
    TEST_ASSERT_TRUE(cfg.interrupt_enabled);
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_EDGE_RISING, cfg.edge);
    TEST_ASSERT_EQUAL_INT(PAL_GPIO_STATUS_BUSY, PAL_GPIO_SetMode(a, PAL_GPIO_OUTPUT));
    cfg.mode = PAL_GPIO_OUTPUT;
    cfg.interrupt_enabled = false;
    OK(PAL_GPIO_SetConfig(a, &cfg));
    OK(PAL_GPIO_SetInterrupt(b, true, PAL_GPIO_EDGE_FALLING));
}

int main(int argc, char **argv)
{
    const TestCase cases[] = {
        TEST_CASE(test_configuration), TEST_CASE(test_validation), TEST_CASE(test_edges), TEST_CASE(test_shared_line)
    };
    return run_test_cases(argc, argv, __FILE__, cases, sizeof cases / sizeof cases[0]);
}
