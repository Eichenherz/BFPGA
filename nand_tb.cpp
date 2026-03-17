#include <cstdio>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#include "Vnand_gate.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

struct wl_context
{
    wl_display*     display;
    wl_registry*    registry;
    wl_compositor*  compositor;
    wl_shm*         shm;
    wl_surface*     surface;
    xdg_wm_base*    xdgBase;
    xdg_surface*    xdgSurf;
    xdg_toplevel*   xdgTop;
};

static void XdgSurfConfig( void *data, struct xdg_surface *surf, uint32_t serial ) 
{
    xdg_surface_ack_configure( surf, serial );
}
 
static const struct xdg_surface_listener xdgSurfListener = {
    .configure = XdgSurfConfig,
};
 
static void XdgTopConfig( void *d, xdg_toplevel *t, int32_t w, int32_t h, wl_array *s ) {}
static void XdgTopClose( void *d, xdg_toplevel *t ) { running = 0; }
 
static const struct xdg_toplevel_listener xdgTopListener = {
    .configure = XdgTopConfig,
    .close     = XdgTopClose,
};
 
// --- xdg_wm_base ping/pong (mandatory or compositor kills you) ---
 
static void XdgBasePing( void *d, xdg_wm_base *base, uint32_t serial ) 
{
    xdg_wm_base_pong( base, serial );
}
 
static const struct xdg_wm_base_listener xdgBaseListener = {
    .ping = XdgBasePing,
};
 
static void WlRegGlobal( void *d, wl_registry *reg, uint32_t name, const char *iface, uint32_t ver ) 
{
    wl_context* wlCtx = ( wl_context* ) d;

    if( !strcmp( iface, wl_compositor_interface.name ) )
    {
        wlCtx->compositor = wl_registry_bind( reg, name, &wl_compositor_interface, 4 );
    }
    else if( !strcmp( iface, wl_shm_interface.name ) )
    {
        wlCtx->shm = wl_registry_bind( reg, name, &wl_shm_interface, 1 );
    }
    else if( !strcmp( iface, xdg_wm_base_interface.name ) ) 
    {
        wlCtx->xdgBase = wl_registry_bind( reg, name, &xdg_wm_base_interface, 1 );
        xdg_wm_base_add_listener( wlCtx->xdgBase, &xdgBaseListener, NULL );
    }
}
 
static const struct wl_registry_listener regListener = {
    .global = WlRegGlobal,
};

void WlInitCtx( wl_context* wlCtx )
{
    wlCtx->display  = wl_display_connect( NULL );
    HT_ASSERT( wlCtx->display );

    wlCtx->registry = wl_display_get_registry( wlCtx->display );
    HT_ASSERT( wlCtx->registry );

    wl_registry_add_listener( wlCtx->registry, &regListener, wlCtx );
    wl_display_roundtrip( wlCtx->display );
    
    HT_ASSERT( wlCtx->compositor );
    HT_ASSERT( wlCtx->shm );
    HT_ASSERT( wlCtx->xdgBase );
 
    wlCtx->surface  = wl_compositor_create_surface( wlCtx->compositor );
    HT_ASSERT( wlCtx->surface );

    wlCtx->xdgSurf = xdg_wm_base_get_xdg_surface( wlCtx->xdgBase, wlCtx->surface );
    HT_ASSERT( wlCtx->xdgSurf );

    xdg_surface_add_listener( wlCtx->xdgSurf, &xdgSurfListener, NULL );
    wlCtx->xdgTop  = xdg_surface_get_toplevel(wlCtx->xdgSurf);
    HT_ASSERT( wlCtx->xdgSxdgTopurf );

    xdg_toplevel_add_listener( wlCtx->xdgTop, &xdgTopListener, NULL );
 
    wl_surface_commit( wlCtx->surface );
    wl_display_roundtrip( wlCtx->display );
}

struct framebuffer
{
    wl_buffer*      wlBuf;
    uint8_t*        data;
    uint32_t        busy;
};


// WL callbacks
static void WlFramebufferRelease( void *data, wl_buffer *wlBuf )  
{
    ( ( framebuffer* ) data )->busy = 0;
}
 
static const wl_buffer_listener wlBuffListener = {
    .release = WlFramebufferRelease,
};

static uint32_t PixelPitchFromFmt( uint32_t fmt )
{
    switch( fmt )
    {
        case WL_SHM_FORMAT_ARGB8888:
        case WL_SHM_FORMAT_XRGB8888:
        case WL_SHM_FORMAT_ABGR8888:
        case WL_SHM_FORMAT_XBGR8888:
            return 4;
        case WL_SHM_FORMAT_RGB565:
        case WL_SHM_FORMAT_BGR565:
            return 2;
        case WL_SHM_FORMAT_RGB888:
        case WL_SHM_FORMAT_BGR888:
            return 3;
        default:
            return 4;
    }
}

static void CreateFrameBuffers( uint64_t width, uint64_t height, uint32_t fmt, uint64_t nBuffering ) 
{
    uint64_t pixelPitch     = PixelPitchFromFmt( fmt );
    uint64_t stride         = width * pixelPitch;
    uint64_t fboSzInBytes   = stride * height;
    uint64_t totalSzInBytes = fboSzInBytes * nBuffering;
 
    int32_t fileDesc        = memfd_create( "fb", MFD_CLOEXEC );
    ftruncate( fileDesc, totalSzInBytes );
 
    uint8_t* poolData       = mmap( NULL, totalSzInBytes, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_NORESERVE, fileDesc, 0 );
 
    wl_shm_pool* wlPool     = wl_shm_create_pool( shm, fileDesc, totalSzInBytes );
 
    for( uint32_t fboi = 0; fboi < nBuffering; fboi++ ) 
    {
        uint32_t currBuffOffsetInBytes = fboi * fboSzInBytes;
        wl_buffer* wlBuf = wl_shm_pool_create_buffer( wlPool, currBuffOffsetInBytes, width, 
            height, stride, fmt );

        buffers[ fboi ] = {
            .wlBuf = wlBuf,
            .data  = poolDta + currBuffOffsetInBytes,
            .busy  = 0
        }
        wl_buffer_add_listener( buffers[ fboi ].wlBuf, &wlBuffListener, &buffers[ fboi ] );
    }

    wl_shm_pool_destroy( wlPool );
    close( fileDesc );
}
 

struct nand_state
{
    uint32_t a : 1;
    uint32_t b : 1;
    uint32_t expected : 1;
};

int main( int argc, char** argv)
{
    wl_context wlCtx;
    WlInitCtx( &wlCtx );

    Verilated::commandArgs( argc, argv );
    Verilated::traceEverOn( true );

    Vnand_gate dut;
    VerilatedVcdC trace;
    dut.trace( &trace, 5 );
    trace.open( "nand_tb.vcd" );

    constexpr nand_state tests[] = {
        {0, 0, 1},
        {0, 1, 1},
        {1, 0, 1},
        {1, 1, 0},
    };

    uint64_t tick = 0;
    uint32_t failures = 0;

    for( uint32_t i = 0; i < 4; i++ ) 
    {
        dut.a = tests[ i ].a;
        dut.b = tests[ i ].b;
        dut.eval();
        trace.dump( tick++ );

        if ( dut.y != tests[ i ].expected ) 
        {
            printf( "FAIL: %d NAND %d = %d (expected %d)\n", 
                tests[i].a, tests[i].b, dut.y, tests[i].expected );
            ++failures;
        }
    }

    trace.close();

    if ( failures == 0 )
    {
        printf( "All tests passed!\n" );
    } 
    else
    {
        printf( "%d test(s) failed.\n", failures );
    }
    
    return failures;
}
