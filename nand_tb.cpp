#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#include "Vnand_gate.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <ht_error.h>
#include <ht_mem_arena.h>
#include <ht_utils.h>

struct framebuffer_t
{
    wl_buffer* wlBuf;
    u8*        data;
    u32        busy;
};


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
    framebuffer_t*  fbos;
    u32             fboCount : 16;  
    u32             running : 1;
    u32             state : 15;
};

static void XdgSurfConfig( void* data, struct xdg_surface* surf, u32 serial ) 
{
    xdg_surface_ack_configure( surf, serial );
}
 
static const struct xdg_surface_listener xdgSurfListener = {
    .configure = XdgSurfConfig,
};
 
static void XdgTopConfig( void* data, xdg_toplevel* t, i32 w, i32 h, wl_array* s ) {}
static void XdgTopClose( void* data, xdg_toplevel* t ) 
{ 
    wl_context* wlCtx = ( wl_context* ) data;
    wlCtx->running = 0;
}
 
static const struct xdg_toplevel_listener xdgTopListener = {
    .configure = XdgTopConfig,
    .close     = XdgTopClose,
};
 
// --- xdg_wm_base ping/pong (mandatory or compositor kills you) ---
 
static void XdgBasePing( void* data, xdg_wm_base* base, u32 serial ) 
{
    xdg_wm_base_pong( base, serial );
}
 
static const struct xdg_wm_base_listener xdgBaseListener = {
    .ping = XdgBasePing,
};
 
static void WlRegGlobal( void* data, wl_registry* reg, u32 name, const char* iface, u32 ver ) 
{
    wl_context* wlCtx = ( wl_context* ) data;

    if( !strcmp( iface, wl_compositor_interface.name ) )
    {
        wlCtx->compositor = ( wl_compositor* ) wl_registry_bind( reg, name, &wl_compositor_interface, 4 );
    }
    else if( !strcmp( iface, wl_shm_interface.name ) )
    {
        wlCtx->shm = ( wl_shm* ) wl_registry_bind( reg, name, &wl_shm_interface, 1 );
    }
    else if( !strcmp( iface, xdg_wm_base_interface.name ) )
    {
        wlCtx->xdgBase = ( xdg_wm_base* ) wl_registry_bind( reg, name, &xdg_wm_base_interface, 1 );
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

    xdg_surface_add_listener( wlCtx->xdgSurf, &xdgSurfListener, wlCtx );
    wlCtx->xdgTop  = xdg_surface_get_toplevel(wlCtx->xdgSurf);
    HT_ASSERT( wlCtx->xdgTop );

    xdg_toplevel_add_listener( wlCtx->xdgTop, &xdgTopListener, wlCtx );
 
    wl_surface_commit( wlCtx->surface );
    wl_display_roundtrip( wlCtx->display );

    wlCtx->running = 1;
}

// WL callbacks
static void WlFramebufferRelease( void* data, wl_buffer* wlBuf )  
{
    ( ( framebuffer_t* ) data )->busy = 0;
}
 
static const wl_buffer_listener wlBuffListener = {
    .release = WlFramebufferRelease,
};

static u32 PixelPitchFromFmt( u32 fmt )
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

static void WlCreateFrameBuffers( 
    wl_context*     pWlCtx,
    virtual_arena*  pArena, 
    u64             width, 
    u64             height, 
    u32             fmt, 
    u64             nBuffering 
) {
    u64 pixelPitch      = PixelPitchFromFmt( fmt );
    u64 stride          = width * pixelPitch;
    u64 fbSzInBytes     = stride * height;
    u64 totalSzInBytes  = fbSzInBytes * nBuffering;
 
    i32 fileDesc        = memfd_create( "fb", MFD_CLOEXEC );
    HT_ASSERT( fileDesc >= 0 );
    ftruncate( fileDesc, totalSzInBytes );
    
    u32 protPerms       = PROT_READ | PROT_WRITE;
    u32 mmapFlags       = MAP_SHARED | MAP_NORESERVE;
    u8* poolData        = ( u8* ) mmap( NULL, totalSzInBytes, protPerms, mmapFlags, fileDesc, 0 );
    HT_ASSERT( poolData );

    wl_shm_pool* wlPool = wl_shm_create_pool( pWlCtx->shm, fileDesc, totalSzInBytes );
    HT_ASSERT( wlPool );

    pWlCtx->fbos = ArenaNewArray<framebuffer_t>( *pArena, nBuffering );
    pWlCtx->fboCount = nBuffering;
    for( u32 fboi = 0; fboi < pWlCtx->fboCount; fboi++ ) 
    {
        u32 currOffsetInBytes = fboi * fbSzInBytes;
        wl_buffer* wlBuf = wl_shm_pool_create_buffer( wlPool, currOffsetInBytes, width, 
            height, stride, fmt );
        
        HT_ASSERT( wlBuf );

        pWlCtx->fbos[ fboi ] = {
            .wlBuf = wlBuf,
            .data  = poolData + currOffsetInBytes,
            .busy  = 0
        };
        wl_buffer_add_listener( pWlCtx->fbos[ fboi ].wlBuf, &wlBuffListener, &pWlCtx->fbos[ fboi ] );
    }

    wl_shm_pool_destroy( wlPool );
    close( fileDesc );
}

bool WlPumpRequestsAndEvents( wl_context* pWlCtx )
{
    wl_display_dispatch_pending( pWlCtx->display );
    wl_display_flush( pWlCtx->display );
    return pWlCtx->running;
}

framebuffer_t* WlGetNextFramebuffer( wl_context* pWlCtx )
{
    for( u32 i = 0; i < pWlCtx->fboCount; i++ )
    {
        framebuffer_t& fbo = pWlCtx->fbos[ i ];
        if ( !fbo.busy ) 
        {
            return &fbo;
        }
    }
        
}

void WlIssuePresent( wl_context* pWlCtx, framebuffer_t* pFbo )
{
    pFbo->busy = 1;
    wl_surface_attach( pWlCtx->surface, pFbo->wlBuf, 0, 0 );
    wl_surface_damage_buffer( pWlCtx->surface, 0, 0, WIDTH, HEIGHT );
    wl_surface_commit( pWlCtx->surface );
}

static buffer_t *get_free_buffer(void) {
    
    return NULL;
}

struct nand_state
{
    u32 a : 1;
    u32 b : 1;
    u32 expected : 1;
};

int main( i32 argc, char** argv )
{
    virtual_arena mainArena = { 1 * GB };

    wl_context* pWlCtx = ArenaNew<wl_context>( mainArena );
    WlInitCtx( pWlCtx );

    WlCreateFrameBuffers( pWlCtx, &mainArena, 1024, 640, WL_SHM_FORMAT_XBGR8888, 3 );

    for( ;; ) 
    {
        if( !WlPumpRequestsAndEvents( pWlCtx ) ) break;
        

        framebuffer_t* pFbo = WlGetNextFramebuffer( pWlCtx );
        
        // === YOUR RENDER HERE ===
        // write XRGB8888 pixels into buf->data, STRIDE bytes per row
        memset(pFbo->data, 0x40, BUF_SIZE);  // placeholder: dark grey
 
       WlIssuePresent( pWlCtx,  );
    }

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

    u64 tick = 0;
    u32 failures = 0;

    for( u32 i = 0; i < 4; i++ ) 
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
