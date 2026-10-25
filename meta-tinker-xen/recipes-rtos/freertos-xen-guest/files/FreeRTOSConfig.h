/*
 * FreeRTOS configuration for a 32-bit ARM Xen guest (ARM_CA9 GCC port).
 *
 * The guest sees Xen's virtual GICv2: distributor at 0x03001000, CPU
 * interface at 0x03002000, and the generic virtual timer on PPI 27.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configCPU_CLOCK_HZ                          24000000UL
#define configTICK_RATE_HZ                          1000
#define configUSE_PREEMPTION                        1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION     1
#define configUSE_TICKLESS_IDLE                     0
#define configMAX_PRIORITIES                        5
#define configMINIMAL_STACK_SIZE                    256
#define configMAX_TASK_NAME_LEN                     16
#define configTICK_TYPE_WIDTH_IN_BITS               TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                     1
#define configUSE_MUTEXES                           1
#define configUSE_RECURSIVE_MUTEXES                 0
#define configUSE_COUNTING_SEMAPHORES               0
#define configQUEUE_REGISTRY_SIZE                   0
#define configUSE_QUEUE_SETS                        0
#define configUSE_TIME_SLICING                      1
#define configUSE_TIMERS                            0
#define configUSE_IDLE_HOOK                         1   /* WFI: let Xen run other domains */
#define configUSE_TICK_HOOK                         0
#define configUSE_MALLOC_FAILED_HOOK                0
#define configCHECK_FOR_STACK_OVERFLOW              0
#define configUSE_TRACE_FACILITY                    0
#define configGENERATE_RUN_TIME_STATS               0
#define configSUPPORT_DYNAMIC_ALLOCATION            1
#define configSUPPORT_STATIC_ALLOCATION             0
#define configTOTAL_HEAP_SIZE                       ( 64 * 1024 )

/* Every task gets an FPU context (the compiler may use VFP registers) */
#define configUSE_TASK_FPU_SUPPORT                  2

/*
 * Interrupts: the GIC-400 virtual CPU interface implements 5 priority bits.
 * configASSERT() is deliberately left undefined: with asserts enabled the
 * port probes the distributor's priority bits, but Xen's emulated
 * distributor stores all 8 bits, so the probe would fail on a correct setup.
 */
#define configUNIQUE_INTERRUPT_PRIORITIES           32
#define configMAX_API_CALL_INTERRUPT_PRIORITY       18
#define configINTERRUPT_CONTROLLER_BASE_ADDRESS     0x03001000UL
#define configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET 0x1000UL

/* Tick from the generic virtual timer (main.c) */
void vSetupTickInterrupt( void );
void vClearTickInterrupt( void );
#define configSETUP_TICK_INTERRUPT()                vSetupTickInterrupt()
#define configCLEAR_TICK_INTERRUPT()                vClearTickInterrupt()

#define INCLUDE_vTaskDelay                          1
#define INCLUDE_xTaskDelayUntil                     1
#define INCLUDE_vTaskDelete                         0
#define INCLUDE_vTaskSuspend                        1
#define INCLUDE_xTaskGetSchedulerState              1
#define INCLUDE_uxTaskPriorityGet                   1

#endif /* FREERTOS_CONFIG_H */
