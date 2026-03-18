#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#include "display_driver/Vdisplay_driver.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <ht_error.h>
#include <ht_mem_arena.h>
#include <ht_utils.h>

// TODO: add "swizzle mask"
struct framebuffer_t
{
    u8*         data;
    u64         width       : 16;
    u64         height      : 16;
    u64         pixelPitch  : 8;
    u64         padding     : 24;
};

void FboClear( framebuffer_t* pFbo, u32 clearVal )
{
    u64 pixelCount = pFbo->width * pFbo->height;
    u32* pPixels   = ( u32* ) pFbo->data;
    for( u64 pi = 0; pi < pixelCount; pi++ )
    {
        pPixels[ pi ] = clearVal;
    }
}

void FboCopyData( framebuffer_t* pDst, framebuffer_t* pSrc )
{
    HT_ASSERT( pDst->width == pSrc->width );
    HT_ASSERT( pDst->height == pSrc->height );
    HT_ASSERT( pDst->pixelPitch == pSrc->pixelPitch );
    u64 buffSz = pSrc->width * pSrc->pixelPitch * pSrc->height;
    memcpy( pDst->data, pSrc->data, buffSz );
}

framebuffer_t CreateFbo(
    virtual_arena*  pArena, 
    u64             width, 
    u64             height, 
    u64             pixelPitch
) {
    u64 stride          = width * pixelPitch;
    u64 fbSzInBytes     = stride * height;

    framebuffer_t fbo   = {
        .data = ArenaNewArray<u8>( *pArena, fbSzInBytes ),
        .width = width,
        .height = height,
        .pixelPitch = pixelPitch
    };

    FboClear( &fbo, 0 );
    return fbo;
}


struct wl_fbo_t
{
    framebuffer_t   fbo;
    wl_buffer*      wlBuf;
    u32             busy;
};

struct wl_context
{
    wl_display*     display;
    wl_registry*    registry;
    wl_compositor*  compositor;
    wl_shm*         shm;
    wl_shm_pool*    shmPool;
    wl_surface*     surface;
    xdg_wm_base*    xdgBase;
    xdg_surface*    xdgSurf;
    xdg_toplevel*   xdgTop;
    wl_fbo_t*       fbos;
    u8*             poolData;
    u64             poolSz;
    i32             poolFd;
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
        wlCtx->compositor = ( wl_compositor* ) wl_registry_bind( reg, name, 
            &wl_compositor_interface, 4 );
    }
    else if( !strcmp( iface, wl_shm_interface.name ) )
    {
        wlCtx->shm = ( wl_shm* ) wl_registry_bind( reg, name, &wl_shm_interface, 1 );
    }
    else if( !strcmp( iface, xdg_wm_base_interface.name ) )
    {
        wlCtx->xdgBase = ( xdg_wm_base* ) wl_registry_bind( reg, name, 
            &xdg_wm_base_interface, 1 );
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

    xdg_toplevel_set_title( wlCtx->xdgTop, "BFG");
    xdg_toplevel_set_app_id( wlCtx->xdgTop, "BFG");

    xdg_toplevel_add_listener( wlCtx->xdgTop, &xdgTopListener, wlCtx );
 
    wl_surface_commit( wlCtx->surface );
    wl_display_roundtrip( wlCtx->display );

    wlCtx->running = 1;
}

// WL callbacks
static void WlFramebufferRelease( void* data, wl_buffer* wlBuf )
{
    wl_fbo_t* pFbo = ( wl_fbo_t* ) data;
    pFbo->busy = 0;
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

    pWlCtx->poolData    = poolData;
    pWlCtx->poolFd      = fileDesc;
    pWlCtx->poolSz      = totalSzInBytes;
    pWlCtx->shmPool     = wl_shm_create_pool( pWlCtx->shm, fileDesc, totalSzInBytes );
    HT_ASSERT( pWlCtx->shmPool );

    pWlCtx->fboCount    = nBuffering;
    pWlCtx->fbos        = ArenaNewArray<wl_fbo_t>( *pArena, pWlCtx->fboCount );

    for( u32 fboi = 0; fboi < pWlCtx->fboCount; fboi++ )
    {
        u32 currOffsetInBytes = fboi * fbSzInBytes;
        wl_fbo_t* pFbo = &pWlCtx->fbos[ fboi ];

        pFbo->fbo = {
            .data       = poolData + currOffsetInBytes,
            .width      = width,
            .height     = height,
            .pixelPitch = pixelPitch
        };
        pFbo->busy = 0;

        pFbo->wlBuf = wl_shm_pool_create_buffer( pWlCtx->shmPool,
            currOffsetInBytes, width, height, stride, fmt );
        HT_ASSERT( pFbo->wlBuf );
        wl_buffer_add_listener( pFbo->wlBuf, &wlBuffListener, pFbo );
    }
}

bool WlPumpEvents( wl_context* pWlCtx )
{
    // NOTE: dsipatch queued events 
    while( 0 != wl_display_prepare_read( pWlCtx->display ) )
    {
        wl_display_dispatch_pending( pWlCtx->display );
    }
    pollfd fds = { .fd = wl_display_get_fd( pWlCtx->display ), .events = POLLIN };
    if( poll( &fds, 1, 0 ) > 0 )
    {
        wl_display_read_events( pWlCtx->display );
    }
    else
    {
        wl_display_cancel_read( pWlCtx->display );
    }
        
    // NOTE: dispatch what we just read
    wl_display_dispatch_pending( pWlCtx->display );
    
    return pWlCtx->running;
}

wl_fbo_t* WlGetNextFramebuffer( wl_context* pWlCtx )
{
    for( u32 i = 0; i < pWlCtx->fboCount; i++ )
    {
        if( !pWlCtx->fbos[ i ].busy ) return &pWlCtx->fbos[ i ];
    }
    return nullptr;
}

void WlIssuePresent( wl_context* pWlCtx, wl_fbo_t* pFbo )
{
    pFbo->busy = 1;
    wl_surface_attach( pWlCtx->surface, pFbo->wlBuf, 0, 0 );
    wl_surface_damage_buffer( pWlCtx->surface, 0, 0, pFbo->fbo.width, pFbo->fbo.height );
    wl_surface_commit( pWlCtx->surface );
    wl_display_flush( pWlCtx->display );
}

int main( i32 argc, char** argv )
{
    virtual_arena mainArena = { 2 * GB };

    wl_context* pWlCtx = ArenaNew<wl_context>( mainArena );
    WlInitCtx( pWlCtx );

    WlCreateFrameBuffers( pWlCtx, &mainArena, 1024, 640, WL_SHM_FORMAT_XRGB8888, 3 );


    Verilated::commandArgs( argc, argv );
    Verilated::traceEverOn( true );

    Vdisplay_driver display;
    VerilatedVcdC trace;
    display.trace( &trace, 4 );
    trace.open( "display_driver_tb.vcd" );

    // reset
    display.simRst = 1;
    display.clkPxl = 0;
    display.eval();
    display.clkPxl = 1;
    display.eval();
    display.simRst = 0;
    display.clkPxl = 0;
    display.eval();

    framebuffer_t displayFbo = CreateFbo( &mainArena, 1024, 640, 4 ); // TODO: don't hardcode
    FboClear( &displayFbo, 0x000000AA );
    for( ;; ) 
    {
        // cycle the clock
        display.clkPxl = 1;
        display.eval();
        display.clkPxl = 0;
        display.eval();

        if( !WlPumpEvents( pWlCtx ) ) break;
        

        wl_fbo_t* pFbo = WlGetNextFramebuffer( pWlCtx );
        if( !pFbo ) continue; // NOTE: shouldn't hit this case with 3+ fbos
        // === YOUR RENDER HERE ===

        FboCopyData( &pFbo->fbo, &displayFbo );

        WlIssuePresent( pWlCtx, pFbo );
    }

    display.final();
    trace.close();

    return 0;
}
