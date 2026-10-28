/*
 * FreeRTOS demo running as a 32-bit ARM Xen guest (domU) on the RK3288.
 *
 * - console: the Xen PV console ring that libxl sets up for every guest,
 *   read by xenconsoled in dom0 ("xl console rtos1"). Before it is set up,
 *   or if it is missing, the CONSOLEIO_write hypercall is used instead; Xen
 *   allows that for a domU only in debug builds (CONFIG_VERBOSE_DEBUG), and
 *   the text then shows up on the Xen serial console prefixed with "(dN)".
 * - tick: generic virtual timer (PPI 27) through Xen's virtual GICv2.
 */
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ---- Xen console ------------------------------------------------------- */

#define __HYPERVISOR_console_io         18
#define __HYPERVISOR_event_channel_op   32
#define __HYPERVISOR_hvm_op             34
#define CONSOLEIO_write                 0
#define EVTCHNOP_send                   4
#define HVMOP_get_param                 1
#define HVM_PARAM_CONSOLE_PFN           17
#define HVM_PARAM_CONSOLE_EVTCHN        18
#define DOMID_SELF                      0x7ff0

struct xen_hvm_param
{
    uint16_t domid;
    uint32_t index;
    uint64_t value;
};

/* public/io/console.h */
struct xencons_interface
{
    char in[ 1024 ];
    char out[ 2048 ];
    uint32_t in_cons, in_prod;
    uint32_t out_cons, out_prod;
};

static volatile struct xencons_interface * pxConsRing;
static uint32_t ulConsEvtchn;

static int32_t xen_hypercall2( uint32_t op, uint32_t a0, uint32_t a1 )
{
    register uint32_t r0 __asm__( "r0" ) = a0;
    register uint32_t r1 __asm__( "r1" ) = a1;
    register uint32_t r12 __asm__( "r12" ) = op;

    /* A debug Xen clobbers the argument registers on return */
    __asm__ volatile ( ".arch_extension virt\n\thvc #0xEA1"
                       : "+r" ( r0 ), "+r" ( r1 ), "+r" ( r12 )
                       :
                       : "r2", "r3", "r4", "memory" );
    return ( int32_t ) r0;
}

static void xen_console_write( const char * s, uint32_t len )
{
    register uint32_t r0 __asm__( "r0" ) = CONSOLEIO_write;
    register uint32_t r1 __asm__( "r1" ) = len;
    register const char * r2 __asm__( "r2" ) = s;
    register uint32_t r12 __asm__( "r12" ) = __HYPERVISOR_console_io;

    /* A debug Xen clobbers the argument registers on return */
    __asm__ volatile ( ".arch_extension virt\n\thvc #0xEA1"
                       : "+r" ( r0 ), "+r" ( r1 ), "+r" ( r2 ), "+r" ( r12 )
                       :
                       : "r3", "memory" );
}

/*
 * Copy one line into the PV console ring and kick dom0. Never blocks: if
 * xenconsoled is not draining the ring, the rest of the line is dropped
 * so the real-time tasks keep running.
 */
static void xencons_write( const char * s, uint32_t len )
{
    volatile struct xencons_interface * ring = pxConsRing;
    uint32_t prod = ring->out_prod;
    uint32_t i;

    __asm__ volatile ( "dmb" ::: "memory" );

    for( i = 0; i < len; i++ )
    {
        /* xl console puts the terminal in raw mode: send CR LF */
        if( s[ i ] == '\n' )
        {
            if( prod - ring->out_cons >= sizeof( ring->out ) )
            {
                break;
            }

            ring->out[ prod++ & ( sizeof( ring->out ) - 1 ) ] = '\r';
        }

        if( prod - ring->out_cons >= sizeof( ring->out ) )
        {
            break;
        }

        ring->out[ prod++ & ( sizeof( ring->out ) - 1 ) ] = s[ i ];
    }

    __asm__ volatile ( "dmb" ::: "memory" );
    ring->out_prod = prod;
    __asm__ volatile ( "dsb" ::: "memory" );

    ( void ) xen_hypercall2( __HYPERVISOR_event_channel_op, EVTCHNOP_send,
                             ( uint32_t ) &ulConsEvtchn );
}

static void console_write( const char * s, uint32_t len )
{
    /* Several tasks print: keep each line in one piece (a short, bounded copy) */
    int locked = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;

    if( locked )
    {
        taskENTER_CRITICAL();
    }

    if( pxConsRing != NULL )
    {
        xencons_write( s, len );
    }
    else
    {
        xen_console_write( s, len );
    }

    if( locked )
    {
        taskEXIT_CRITICAL();
    }
}

/*
 * Read whatever dom0 has written to this guest's console (xenconsoled
 * forwards the console pty's input into the ring). Never blocks.
 */
static uint32_t console_read( char * buf, uint32_t size )
{
    volatile struct xencons_interface * ring = pxConsRing;
    uint32_t cons, prod, n = 0;

    if( ring == NULL )
    {
        return 0;
    }

    cons = ring->in_cons;
    prod = ring->in_prod;
    __asm__ volatile ( "dmb" ::: "memory" );

    while( cons != prod && n < size )
    {
        buf[ n++ ] = ring->in[ cons++ & ( sizeof( ring->in ) - 1 ) ];
    }

    __asm__ volatile ( "dmb" ::: "memory" );
    ring->in_cons = cons;
    __asm__ volatile ( "dsb" ::: "memory" );

    /* Tell xenconsoled there is room again, but only when the ring was nearly
     * full: it refills a ring with free space by itself, and it throttles a
     * guest that notifies more than 30 times in 200 ms (that throttling held
     * vehicle frames back for up to 105 ms). */
    if( n && ( prod - ( cons - n ) ) > sizeof( ring->in ) - 256 )
    {
        ( void ) xen_hypercall2( __HYPERVISOR_event_channel_op, EVTCHNOP_send,
                                 ( uint32_t ) &ulConsEvtchn );
    }

    return n;
}

static int32_t hvm_get_param( uint32_t index, uint64_t * value )
{
    struct xen_hvm_param p = { .domid = DOMID_SELF, .index = index, .value = 0 };
    int32_t rc = xen_hypercall2( __HYPERVISOR_hvm_op, HVMOP_get_param, ( uint32_t ) &p );

    *value = p.value;
    return rc;
}

/* ---- minimal libc ------------------------------------------------------ */

void * memset( void * dst, int c, size_t n )
{
    uint8_t * d = dst;

    while( n-- )
    {
        *d++ = ( uint8_t ) c;
    }

    return dst;
}

void * memcpy( void * dst, const void * src, size_t n )
{
    uint8_t * d = dst;
    const uint8_t * s = src;

    while( n-- )
    {
        *d++ = *s++;
    }

    return dst;
}

static void put_char( char * buf, uint32_t * pos, uint32_t size, char c )
{
    if( *pos < size - 1 )
    {
        buf[ ( *pos )++ ] = c;
    }
}

static void put_uint( char * buf, uint32_t * pos, uint32_t size,
                      uint32_t v, uint32_t base )
{
    char tmp[ 11 ];
    int n = 0;

    do
    {
        uint32_t d = v % base;
        tmp[ n++ ] = ( char ) ( d < 10 ? '0' + d : 'a' + d - 10 );
        v /= base;
    } while( v != 0 );

    while( n > 0 )
    {
        put_char( buf, pos, size, tmp[ --n ] );
    }
}

/* printf subset: %s %c %u %d %x %%; one hypercall per call */
static void rtos_printf( const char * fmt, ... )
{
    char buf[ 200 ];
    uint32_t pos = 0;
    va_list ap;

    va_start( ap, fmt );

    for( ; *fmt != '\0'; fmt++ )
    {
        if( *fmt != '%' )
        {
            put_char( buf, &pos, sizeof( buf ), *fmt );
            continue;
        }

        switch( *++fmt )
        {
            case 's':
            {
                const char * s = va_arg( ap, const char * );

                while( *s != '\0' )
                {
                    put_char( buf, &pos, sizeof( buf ), *s++ );
                }

                break;
            }

            case 'c':
                put_char( buf, &pos, sizeof( buf ), ( char ) va_arg( ap, int ) );
                break;

            case 'u':
                put_uint( buf, &pos, sizeof( buf ), va_arg( ap, uint32_t ), 10 );
                break;

            case 'd':
            {
                int32_t v = va_arg( ap, int32_t );

                if( v < 0 )
                {
                    put_char( buf, &pos, sizeof( buf ), '-' );
                    v = -v;
                }

                put_uint( buf, &pos, sizeof( buf ), ( uint32_t ) v, 10 );
                break;
            }

            case 'x':
                put_uint( buf, &pos, sizeof( buf ), va_arg( ap, uint32_t ), 16 );
                break;

            case '%':
                put_char( buf, &pos, sizeof( buf ), '%' );
                break;

            default:
                fmt--;
                break;
        }
    }

    va_end( ap );

    /* One write per line: Xen buffers a domU's CONSOLEIO text until '\n' */
    console_write( buf, pos );
}

/* ---- MMU and caches ---------------------------------------------------- */

/*
 * Identity map with 1 MB sections: guest RAM is normal write-back memory,
 * everything else is device memory. With the MMU off every guest access is
 * uncached while Xen reads guest memory (e.g. the console buffer) through
 * its cache, so Xen would see stale data.
 */
#define GUEST_RAM_BASE      0x40000000UL
#define GUEST_RAM_MB        4   /* memory in rtos1.cfg */
#define SECTION_DEVICE      0x00c16UL   /* AP=RW, XN, shareable device (TEX 0, C 0, B 1) */
#define SECTION_NORMAL      0x11c0eUL   /* AP=RW, S, write-back write-allocate (TEX 1, C 1, B 1) */

static uint32_t ulL1Table[ 4096 ] __attribute__( ( aligned( 16384 ) ) );
extern uint8_t _start[], _end[];

static void mmu_enable( void )
{
    uint32_t ctr, line, sctlr, i;
    uintptr_t p;

    for( i = 0; i < 4096; i++ )
    {
        ulL1Table[ i ] = ( i << 20 ) | SECTION_DEVICE;
    }

    for( i = 0; i < GUEST_RAM_MB; i++ )
    {
        uint32_t mb = ( GUEST_RAM_BASE >> 20 ) + i;
        ulL1Table[ mb ] = ( mb << 20 ) | SECTION_NORMAL;
    }

    /*
     * Everything written so far (page table, .bss, stacks) went straight to
     * RAM. Drop any cache lines for our memory left over from when Xen
     * loaded the image, so they cannot hide that data once caches are on.
     */
    __asm__ volatile ( "mrc p15, 0, %0, c0, c0, 1" : "=r" ( ctr ) );
    line = 4UL << ( ( ctr >> 16 ) & 0xf );

    for( p = ( uintptr_t ) _start & ~( line - 1 ); p < ( uintptr_t ) _end; p += line )
    {
        __asm__ volatile ( "mcr p15, 0, %0, c7, c6, 1" ::"r" ( p ) );   /* DCIMVAC */
    }

    __asm__ volatile ( "dsb" );

    __asm__ volatile ( "mcr p15, 0, %0, c2, c0, 2" ::"r" ( 0 ) );       /* TTBCR: short descriptors, TTBR0 only */
    __asm__ volatile ( "mcr p15, 0, %0, c3, c0, 0" ::"r" ( 0x55555555 ) ); /* DACR: all client */
    __asm__ volatile ( "mcr p15, 0, %0, c2, c0, 0" ::"r" ( ( uintptr_t ) ulL1Table | 0x4a ) ); /* TTBR0, WBWA walks */
    __asm__ volatile ( "mcr p15, 0, %0, c8, c7, 0" ::"r" ( 0 ) );       /* TLBIALL */
    __asm__ volatile ( "mcr p15, 0, %0, c7, c5, 0" ::"r" ( 0 ) );       /* ICIALLU */
    __asm__ volatile ( "mcr p15, 0, %0, c7, c5, 6" ::"r" ( 0 ) );       /* BPIALL */
    __asm__ volatile ( "dsb\n\tisb" );

    __asm__ volatile ( "mrc p15, 0, %0, c1, c0, 0" : "=r" ( sctlr ) );
    sctlr |= ( 1UL << 0 ) | ( 1UL << 2 ) | ( 1UL << 11 ) | ( 1UL << 12 ); /* M, C, Z, I */
    sctlr &= ~( 1UL << 1 );                                             /* no alignment faults */
    __asm__ volatile ( "mcr p15, 0, %0, c1, c0, 0\n\tisb" ::"r" ( sctlr ) );
}

/* Remap the 1 MB section at addr as normal memory (MMU already on) */
static void mmu_map_normal( uintptr_t addr )
{
    uint32_t mb = addr >> 20;

    ulL1Table[ mb ] = ( mb << 20 ) | SECTION_NORMAL;
    __asm__ volatile ( "mcr p15, 0, %0, c7, c10, 1" ::"r" ( &ulL1Table[ mb ] ) ); /* DCCMVAC */
    __asm__ volatile ( "dsb" );
    __asm__ volatile ( "mcr p15, 0, %0, c8, c7, 1" ::"r" ( mb << 20 ) );          /* TLBIMVA */
    __asm__ volatile ( "mcr p15, 0, %0, c7, c5, 6" ::"r" ( 0 ) );                 /* BPIALL */
    __asm__ volatile ( "dsb\n\tisb" );
}

/*
 * Switch the console to the PV ring libxl gave this guest. Xen and
 * xenconsoled access the ring page cached, so it is mapped as normal
 * memory like guest RAM.
 */
static void console_init( void )
{
    uint64_t pfn, evtchn;

    if( ( hvm_get_param( HVM_PARAM_CONSOLE_PFN, &pfn ) != 0 ) || ( pfn == 0 ) ||
        ( hvm_get_param( HVM_PARAM_CONSOLE_EVTCHN, &evtchn ) != 0 ) )
    {
        xen_console_write( "[rtos] no PV console, using CONSOLEIO\n", 38 );
        return;
    }

    mmu_map_normal( ( uintptr_t ) ( pfn << 12 ) );
    ulConsEvtchn = ( uint32_t ) evtchn;
    pxConsRing = ( volatile struct xencons_interface * ) ( uintptr_t ) ( pfn << 12 );
}

/* ---- faults (called from startup.S) ------------------------------------ */

void fault_report( uint32_t type, uint32_t pc )
{
    static const char * const names[] =
    {
        "?", "undefined instruction", "?", "prefetch abort", "data abort",
        "?", "?", "FIQ"
    };

    rtos_printf( "[rtos] FATAL: %s at pc=0x%x\n", names[ type & 7 ], pc );

    for( ; ; )
    {
        __asm__ volatile ( "wfi" );
    }
}

/* ---- virtual GIC and virtual timer tick -------------------------------- */

#define GICD_BASE               0x03001000UL
#define GICC_BASE               0x03002000UL
#define GICD_CTLR               ( *( volatile uint32_t * ) ( GICD_BASE + 0x000 ) )
#define GICD_ISENABLER0         ( *( volatile uint32_t * ) ( GICD_BASE + 0x100 ) )
#define GICD_IPRIORITYR( n )    ( *( volatile uint8_t * ) ( GICD_BASE + 0x400 + ( n ) ) )
#define GICC_CTLR               ( *( volatile uint32_t * ) ( GICC_BASE + 0x00 ) )
#define GICC_PMR                ( *( volatile uint32_t * ) ( GICC_BASE + 0x04 ) )
#define GICC_BPR                ( *( volatile uint32_t * ) ( GICC_BASE + 0x08 ) )

#define VTIMER_PPI              27

static uint32_t ulTimerReload;
static uint64_t ullNextTick;

static inline uint32_t read_cntfrq( void )
{
    uint32_t v;

    __asm__ volatile ( "mrc p15, 0, %0, c14, c0, 0" : "=r" ( v ) );
    return v;
}

static inline uint64_t read_cntvct( void )
{
    uint32_t lo, hi;

    __asm__ volatile ( "isb\n\tmrrc p15, 1, %0, %1, c14" : "=r" ( lo ), "=r" ( hi ) );
    return ( ( uint64_t ) hi << 32 ) | lo;
}

static inline void write_cntv_cval( uint64_t v )
{
    __asm__ volatile ( "mcrr p15, 3, %0, %1, c14" ::"r" ( ( uint32_t ) v ), "r" ( ( uint32_t ) ( v >> 32 ) ) );
}

static inline void write_cntv_ctl( uint32_t v )
{
    __asm__ volatile ( "mcr p15, 0, %0, c14, c3, 1\n\tisb" ::"r" ( v ) );
}

static void gic_init( void )
{
    GICD_CTLR = 1;      /* distributor on */
    GICC_PMR = 0xff;    /* do not mask any priority */
    GICC_BPR = 0;       /* lowest binary point, required by the port */
    GICC_CTLR = 1;      /* CPU interface on */
}

void vSetupTickInterrupt( void )
{
    ulTimerReload = read_cntfrq() / configTICK_RATE_HZ;

    GICD_IPRIORITYR( VTIMER_PPI ) =
        portLOWEST_USABLE_INTERRUPT_PRIORITY << portPRIORITY_SHIFT;
    GICD_ISENABLER0 = 1UL << VTIMER_PPI;

    ullNextTick = read_cntvct() + ulTimerReload;
    write_cntv_cval( ullNextTick );
    write_cntv_ctl( 1 );    /* enable, interrupt not masked */
}

void vClearTickInterrupt( void )
{
    /*
     * Absolute compare value, so interrupt latency does not add up from tick
     * to tick. Re-enable: Xen masks the timer (IMASK) when it injects it.
     */
    ullNextTick += ulTimerReload;
    write_cntv_cval( ullNextTick );
    write_cntv_ctl( 1 );
}

/* Give the physical CPU back to Xen while FreeRTOS has nothing to do */
void vApplicationIdleHook( void )
{
    __asm__ volatile ( "dsb\n\twfi" );
}

static volatile uint32_t ulSpuriousIrqs;

void vApplicationFPUSafeIRQHandler( uint32_t ulICCIAR )
{
    uint32_t id = ulICCIAR & 0x3ffUL;

    if( id == VTIMER_PPI )
    {
        FreeRTOS_Tick_Handler();
    }
    else
    {
        ulSpuriousIrqs++;
    }
}

/* ---- demo tasks -------------------------------------------------------- */

static QueueHandle_t xQueue;

/*
 * Only the real-time task prints: one summary line every REPORT_PERIODS
 * periods (10 s), so the shared Xen console stays usable. The other tasks
 * just count, and the summary reports their counters.
 */
#define REPORT_PERIODS      1000

static volatile uint32_t ulHeartbeats;
static volatile uint32_t ulMessages;
static volatile uint32_t ulLastMinUs, ulLastMaxUs, ulReports;   /* last summary, for the shell */

/* Highest priority: 10 ms period, measure the real period with CNTVCT */
static void vRealTimeTask( void * pv )
{
    const uint32_t ticks_per_us = read_cntfrq() / 1000000UL;
    TickType_t xLastWake = xTaskGetTickCount();
    uint64_t prev = read_cntvct();
    uint32_t min_us = 0xffffffffUL, max_us = 0, cycles = 0, reports = 0;

    ( void ) pv;

    for( ; ; )
    {
        xTaskDelayUntil( &xLastWake, pdMS_TO_TICKS( 10 ) );

        uint64_t now = read_cntvct();
        uint32_t us = ( uint32_t ) ( now - prev ) / ticks_per_us;
        prev = now;

        if( us < min_us )
        {
            min_us = us;
        }

        if( us > max_us )
        {
            max_us = us;
        }

        if( ++cycles == REPORT_PERIODS )
        {
            ulLastMinUs = min_us;
            ulLastMaxUs = max_us;
            ulReports = reports + 1;
            rtos_printf( "[rtos] 10 ms task: %u periods, min %u us, max %u us; "
                         "heartbeats %u, queue messages %u (report %u)\n",
                         REPORT_PERIODS, min_us, max_us, ulHeartbeats, ulMessages,
                         ++reports );
            cycles = 0;
            min_us = 0xffffffffUL;
            max_us = 0;
        }
    }
}

static void vHeartbeatTask( void * pv )
{
    TickType_t xLastWake = xTaskGetTickCount();

    ( void ) pv;

    for( ; ; )
    {
        xTaskDelayUntil( &xLastWake, pdMS_TO_TICKS( 1000 ) );
        ulHeartbeats++;
    }
}

static void vProducerTask( void * pv )
{
    uint32_t n = 0;

    ( void ) pv;

    for( ; ; )
    {
        vTaskDelay( pdMS_TO_TICKS( 2500 ) );
        n++;
        xQueueSend( xQueue, &n, portMAX_DELAY );
    }
}

static void vConsumerTask( void * pv )
{
    uint32_t v;

    ( void ) pv;

    for( ; ; )
    {
        if( xQueueReceive( xQueue, &v, portMAX_DELAY ) == pdPASS )
        {
            ulMessages = v;
        }
    }
}

/* ---- vehicle signals (automotive bench) -------------------------------- */

/*
 * dom0's can2rtos gateway decodes the vehicle CAN bus (read-only) and sends
 * one line per speed frame over this guest's console:
 *     "\036V <seq> <speed in 0.01 km/h> <rpm>\n"
 * The console's input also carries what a person types in "xl console
 * rtos1". can2rtos writes each frame with one write() to the pty, so a frame
 * arrives in one piece; the leading RS byte (036) marks it. Everything
 * outside a frame goes to the shell task.
 * This task checks the signals in real time and reports alerts. It only
 * watches and warns; nothing is sent back to the vehicle.
 */
#define VEHICLE_PERIOD_MS       5
#define VEHICLE_TIMEOUT_MS      100     /* no frame for this long: signal lost */
#define VEHICLE_REPORT_S        10
#define OVERSPEED_KMH100        12000   /* 120.00 km/h */
#define REDLINE_RPM             6000
#define FRAME_START             '\036'

/* alert limits (the shell can change them) and the latest state for the shell */
static volatile uint32_t ulOverspeedLimit = OVERSPEED_KMH100;
static volatile uint32_t ulRedlineLimit = REDLINE_RPM;
static volatile uint32_t ulSpeed, ulRpm, ulHaveSignal, ulSeen, ulFramesTotal, ulLostTotal;
static volatile uint32_t ulOverspeedAlerts, ulRedlineAlerts, ulSignalLosses;

static QueueHandle_t xShellQueue;

static int parse_uint( const char ** p, uint32_t * v )
{
    const char * s = *p;
    uint32_t x = 0;

    while( *s == ' ' )
    {
        s++;
    }

    if( *s < '0' || *s > '9' )
    {
        return 0;
    }

    while( *s >= '0' && *s <= '9' )
    {
        x = x * 10 + ( uint32_t ) ( *s++ - '0' );
    }

    *p = s;
    *v = x;
    return 1;
}

static void vVehicleTask( void * pv )
{
    const uint32_t ticks_per_us = read_cntfrq() / 1000000UL;
    TickType_t xLastWake = xTaskGetTickCount();
    char line[ 64 ], buf[ 256 ];
    uint32_t len = 0, i, n, iter = 0;
    uint32_t speed = 0, rpm = 0, last_seq = 0, frames = 0, lost = 0, bad = 0;
    uint32_t max_gap_us = 0, signal_losses = 0, overspeed_alerts = 0, redline_alerts = 0;
    uint64_t last_rx = 0;
    int have_signal = 0, seen = 0, overspeed = 0, redline = 0, in_frame = 0;

    ( void ) pv;

    for( ; ; )
    {
        uint64_t now;

        xTaskDelayUntil( &xLastWake, pdMS_TO_TICKS( VEHICLE_PERIOD_MS ) );
        now = read_cntvct();

        n = console_read( buf, sizeof( buf ) );

        for( i = 0; i < n; i++ )
        {
            char c = buf[ i ];

            if( c == FRAME_START )
            {
                in_frame = 1;
                len = 0;
                continue;
            }

            if( !in_frame )
            {
                /* typed input: never wait, the shell drops keys it cannot take */
                ( void ) xQueueSend( xShellQueue, &c, 0 );
                continue;
            }

            if( c != '\n' && c != '\r' )
            {
                if( len < sizeof( line ) - 1 )
                {
                    line[ len++ ] = c;
                }

                continue;
            }

            line[ len ] = '\0';
            in_frame = 0;

            if( len > 2 && line[ 0 ] == 'V' && line[ 1 ] == ' ' )
            {
                const char * p = line + 1;
                uint32_t seq, sp, r;

                if( parse_uint( &p, &seq ) && parse_uint( &p, &sp ) && parse_uint( &p, &r ) )
                {
                    if( seen && seq != last_seq + 1 && seq > last_seq )
                    {
                        lost += seq - last_seq - 1;
                        ulLostTotal += seq - last_seq - 1;
                    }

                    if( have_signal )
                    {
                        uint32_t gap = ( uint32_t ) ( now - last_rx ) / ticks_per_us;

                        if( gap > max_gap_us )
                        {
                            max_gap_us = gap;
                        }
                    }
                    else if( seen )
                    {
                        rtos_printf( "[vehicle] SIGNAL OK again (seq %u)\n", seq );
                    }
                    else
                    {
                        rtos_printf( "[vehicle] first vehicle frame (seq %u)\n", seq );
                    }

                    last_seq = seq;
                    speed = sp;
                    rpm = r;
                    last_rx = now;
                    have_signal = 1;
                    seen = 1;
                    frames++;
                    ulFramesTotal++;

                    if( !overspeed && speed > ulOverspeedLimit )
                    {
                        overspeed = 1;
                        overspeed_alerts++;
                        rtos_printf( "[vehicle] ALERT overspeed: %u km/h (limit %u)\n",
                                     speed / 100, ulOverspeedLimit / 100 );
                    }
                    else if( overspeed && speed < ulOverspeedLimit - 500 )
                    {
                        overspeed = 0;
                        rtos_printf( "[vehicle] overspeed cleared: %u km/h\n", speed / 100 );
                    }

                    if( !redline && rpm > ulRedlineLimit )
                    {
                        redline = 1;
                        redline_alerts++;
                        rtos_printf( "[vehicle] ALERT engine over redline: %u rpm (limit %u)\n",
                                     rpm, ulRedlineLimit );
                    }
                    else if( redline && rpm < ulRedlineLimit - 300 )
                    {
                        redline = 0;
                        rtos_printf( "[vehicle] redline cleared: %u rpm\n", rpm );
                    }
                }
                else
                {
                    bad++;
                }
            }

            len = 0;
        }

        /* fail-safe: stale data is not shown as valid */
        if( have_signal &&
            ( uint32_t ) ( now - last_rx ) / ticks_per_us > VEHICLE_TIMEOUT_MS * 1000UL )
        {
            have_signal = 0;
            signal_losses++;
            rtos_printf( "[vehicle] SIGNAL LOST: no frame for %u ms, speed/rpm invalid\n",
                         VEHICLE_TIMEOUT_MS );
        }

        ulSpeed = speed;
        ulRpm = rpm;
        ulHaveSignal = have_signal;
        ulSeen = seen;
        ulOverspeedAlerts = overspeed_alerts;
        ulRedlineAlerts = redline_alerts;
        ulSignalLosses = signal_losses;

        if( ++iter == VEHICLE_REPORT_S * 1000 / VEHICLE_PERIOD_MS )
        {
            iter = 0;

            if( seen )
            {
                rtos_printf( "[vehicle] %us: frames %u, lost %u, bad %u, max gap %u us, "
                             "%s speed %u km/h, rpm %u; alerts: overspeed %u, redline %u, "
                             "signal lost %u\n",
                             VEHICLE_REPORT_S, frames, lost, bad, max_gap_us,
                             have_signal ? "" : "NO SIGNAL,", speed / 100, rpm,
                             overspeed_alerts, redline_alerts, signal_losses );
            }

            frames = lost = bad = max_gap_us = 0;
        }
    }
}

/* ---- shell (xl console rtos1) ------------------------------------------ */

/*
 * A few commands for the person at "xl console rtos1". Lowest priority: it
 * only runs when the real-time tasks have nothing to do. Echo is batched
 * (one console write per burst of keys): xenconsoled throttles a guest
 * that notifies too often, and that would delay the vehicle frames.
 */
#define SHELL_PROMPT    "rtos> "

static int str_eq( const char * a, const char * b )
{
    while( *a != '\0' && *a == *b )
    {
        a++;
        b++;
    }

    return *a == *b;
}

/* next space-separated word of *p, made into its own string */
static char * next_word( char ** p )
{
    char * s = *p, * w;

    while( *s == ' ' )
    {
        s++;
    }

    w = s;

    while( *s != '\0' && *s != ' ' )
    {
        s++;
    }

    if( *s != '\0' )
    {
        *s++ = '\0';
    }

    *p = s;
    return w;
}

static void shell_limits( void )
{
    rtos_printf( "limits: overspeed above %u km/h, redline above %u rpm\n",
                 ulOverspeedLimit / 100, ulRedlineLimit );
}

static void shell_command( char * line )
{
    char * p = line, * cmd = next_word( &p );

    if( *cmd == '\0' )
    {
        return;
    }

    if( str_eq( cmd, "help" ) )
    {
        static const char help[] =
            "commands:\n"
            "  status              vehicle signal, speed, rpm, alerts\n"
            "  rt                  10 ms real-time task timing\n"
            "  tasks               tasks and free memory\n"
            "  uptime              time since FreeRTOS started\n"
            "  limits              alert limits\n"
            "  limit speed <km/h>  set the overspeed limit (10-300)\n"
            "  limit rpm <rpm>     set the redline (1000-10000)\n"
            "  clear               clear the screen\n"
            "leave the console with Ctrl-]\n";

        console_write( help, sizeof( help ) - 1 );
    }
    else if( str_eq( cmd, "status" ) )
    {
        if( !ulSeen )
        {
            rtos_printf( "vehicle: no frame received yet\n" );
        }
        else
        {
            rtos_printf( "vehicle: %s, speed %u km/h, rpm %u\n",
                         ulHaveSignal ? "signal OK" : "NO SIGNAL (values are old)",
                         ulSpeed / 100, ulRpm );
        }

        rtos_printf( "frames: %u received, %u lost; alerts: overspeed %u, redline %u, "
                     "signal lost %u\n", ulFramesTotal, ulLostTotal, ulOverspeedAlerts,
                     ulRedlineAlerts, ulSignalLosses );
        shell_limits();
    }
    else if( str_eq( cmd, "rt" ) )
    {
        if( ulReports == 0 )
        {
            rtos_printf( "rt: first report after 10 s\n" );
        }
        else
        {
            rtos_printf( "rt: 10 ms task, last %u periods: min %u us, max %u us "
                         "(report %u)\n", REPORT_PERIODS, ulLastMinUs, ulLastMaxUs,
                         ulReports );
        }
    }
    else if( str_eq( cmd, "tasks" ) )
    {
        rtos_printf( "tasks: %u (rt10ms 4, heartbeat 3, vehicle 3, producer 2, consumer 2, "
                     "shell 1, idle 0)\n", ( uint32_t ) uxTaskGetNumberOfTasks() );
        rtos_printf( "heap: %u bytes free, lowest ever %u, of %u\n",
                     ( uint32_t ) xPortGetFreeHeapSize(),
                     ( uint32_t ) xPortGetMinimumEverFreeHeapSize(),
                     ( uint32_t ) configTOTAL_HEAP_SIZE );
    }
    else if( str_eq( cmd, "uptime" ) )
    {
        uint32_t s = xTaskGetTickCount() / configTICK_RATE_HZ;

        rtos_printf( "up %u h %u min %u s\n", s / 3600, s / 60 % 60, s % 60 );
    }
    else if( str_eq( cmd, "limits" ) )
    {
        shell_limits();
    }
    else if( str_eq( cmd, "limit" ) )
    {
        char * what = next_word( &p ), * num = next_word( &p );
        const char * q = num;
        uint32_t v;

        if( !parse_uint( &q, &v ) || *q != '\0' )
        {
            rtos_printf( "usage: limit speed <km/h> | limit rpm <rpm>\n" );
        }
        else if( str_eq( what, "speed" ) && v >= 10 && v <= 300 )
        {
            ulOverspeedLimit = v * 100;
            rtos_printf( "[shell] overspeed limit set to %u km/h\n", v );
        }
        else if( str_eq( what, "rpm" ) && v >= 1000 && v <= 10000 )
        {
            ulRedlineLimit = v;
            rtos_printf( "[shell] redline set to %u rpm\n", v );
        }
        else
        {
            rtos_printf( "limit: speed 10-300 km/h, rpm 1000-10000\n" );
        }
    }
    else if( str_eq( cmd, "clear" ) )
    {
        console_write( "\033[2J\033[H", 7 );
    }
    else
    {
        rtos_printf( "unknown command '%s', type help\n", cmd );
    }
}

static void vShellTask( void * pv )
{
    char line[ 64 ], echo[ 80 ], c, prev = 0;
    uint32_t len = 0, e;

    ( void ) pv;

    for( ; ; )
    {
        int run = 0;

        ( void ) xQueueReceive( xShellQueue, &c, portMAX_DELAY );
        e = 0;

        /* this key and any others already waiting, echoed in one write */
        do
        {
            if( c == '\r' || c == '\n' )
            {
                if( !( c == '\n' && prev == '\r' ) )    /* CR LF is one Enter */
                {
                    echo[ e++ ] = '\n';
                    run = 1;
                }
            }
            else if( c == '\b' || c == 0x7f )
            {
                if( len > 0 )
                {
                    len--;
                    echo[ e++ ] = '\b';
                    echo[ e++ ] = ' ';
                    echo[ e++ ] = '\b';
                }
            }
            else if( c == 0x03 )                        /* Ctrl-C: drop the line */
            {
                len = 0;
                echo[ e++ ] = '^';
                echo[ e++ ] = 'C';
                echo[ e++ ] = '\n';
                run = 1;
            }
            else if( c >= ' ' && c <= '~' && len < sizeof( line ) - 1 )
            {
                line[ len++ ] = c;
                echo[ e++ ] = c;
            }

            prev = c;
        } while( !run && e < sizeof( echo ) - 3 &&
                 xQueueReceive( xShellQueue, &c, 0 ) == pdPASS );

        if( e > 0 )
        {
            console_write( echo, e );
        }

        if( run )
        {
            line[ len ] = '\0';
            shell_command( line );
            len = 0;
            console_write( SHELL_PROMPT, sizeof( SHELL_PROMPT ) - 1 );
        }
    }
}

int main( void )
{
    mmu_enable();
    console_init();

    rtos_printf( "\n[rtos] FreeRTOS %s starting as a Xen guest (ARMv7-A, RK3288)\n",
                 tskKERNEL_VERSION_NUMBER );
    rtos_printf( "[rtos] virtual timer %u Hz, tick %u Hz\n", read_cntfrq(),
                 ( uint32_t ) configTICK_RATE_HZ );

    gic_init();

    xQueue = xQueueCreate( 4, sizeof( uint32_t ) );
    xShellQueue = xQueueCreate( 128, sizeof( char ) );

    xTaskCreate( vRealTimeTask, "rt10ms", configMINIMAL_STACK_SIZE * 2, NULL, 4, NULL );
    xTaskCreate( vHeartbeatTask, "heartbeat", configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL );
    xTaskCreate( vProducerTask, "producer", configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL );
    xTaskCreate( vConsumerTask, "consumer", configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL );
    xTaskCreate( vVehicleTask, "vehicle", configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL );
    xTaskCreate( vShellTask, "shell", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL );

    rtos_printf( "[rtos] 6 tasks created, starting the scheduler; type help for commands\n" );
    vTaskStartScheduler();

    rtos_printf( "[rtos] FATAL: scheduler did not start\n" );

    for( ; ; )
    {
    }
}
