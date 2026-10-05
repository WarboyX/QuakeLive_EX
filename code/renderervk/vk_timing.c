/*
===========================================================================
[QL] vk_timing.c - r_rtTimings: GPU time per ray-tracing pass (E177).
Split out of vk.c unchanged.
===========================================================================
*/
#include "vk_local.h"



/*
=================
[QL] E177. GPU time per ray-tracing pass (r_rtTimings 1).

"The shadows cost a lot" had nothing to measure it with. A timestamp at the
start and the end of each pass, per command buffer, read back when that
command buffer's fence has been waited on anyway (vk_begin_frame), averaged
and printed every two seconds. Each section is written at most once a frame -
its first begin and its first end - so a pass that runs twice is not double
counted and a query is never written twice without a reset.
=================
*/
static const char *const rttNames[RTT_COUNT] = {
	"whole frame", "structure build", "ambient occlusion", "shadows on the level",
	"water reflections", "lit surfaces (dynamic lights)",
	"[AO trace", "AO composite", "shadow trace", "shadow composite]"
};
static VkQueryPool rttPool = VK_NULL_HANDLE;
static float rttPeriodNs;
static byte rttState[NUM_COMMAND_BUFFERS][RTT_COUNT];   /* bit 0 begun, bit 1 ended */
static double rttSum[RTT_COUNT];
static int rttFrames, rttLastPrint;

void vk_timing_create( const VkPhysicalDeviceProperties *props )
{
	VkQueryPoolCreateInfo desc;

	rttPool = VK_NULL_HANDLE;
	if ( !qvkCreateQueryPool || props->limits.timestampPeriod <= 0.0f || !props->limits.timestampComputeAndGraphics ) {
		ri.Printf( PRINT_ALL, "RT timings: not available - this device has no graphics-queue timestamps\n" );
		return;
	}
	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	desc.queryType = VK_QUERY_TYPE_TIMESTAMP;
	desc.queryCount = NUM_COMMAND_BUFFERS * RTT_COUNT * 2;
	if ( qvkCreateQueryPool( vk.device, &desc, NULL, &rttPool ) != VK_SUCCESS ) {
		rttPool = VK_NULL_HANDLE;
		return;
	}
	rttPeriodNs = props->limits.timestampPeriod;
	ri.Printf( PRINT_ALL, "RT timings: available (r_rtTimings 1 prints them)\n" );
	Com_Memset( rttState, 0, sizeof( rttState ) );
	Com_Memset( rttSum, 0, sizeof( rttSum ) );
	rttFrames = 0;
}

void vk_timing_destroy( void )
{
	if ( rttPool != VK_NULL_HANDLE ) {
		qvkDestroyQueryPool( vk.device, rttPool, NULL );
		rttPool = VK_NULL_HANDLE;
	}
}

/* at the top of a frame's command buffer: collect what it measured last time, then reset */
void vk_timing_frame_start( void )
{
	const int base = vk.cmd_index * RTT_COUNT * 2;
	int sct;

	if ( rttPool == VK_NULL_HANDLE ) {
		return;
	}
	if ( rttState[ vk.cmd_index ][ RTT_FRAME ] == 3 ) {
		/* section by section: a pass that did not run left its two queries
		   unwritten, and asking for the whole range would get VK_NOT_READY
		   for all of it */
		for ( sct = 0; sct < RTT_COUNT; sct++ ) {
			uint64_t ts[2];
			if ( rttState[ vk.cmd_index ][ sct ] != 3 ) {
				continue;
			}
			if ( qvkGetQueryPoolResults( vk.device, rttPool, base + sct * 2, 2, sizeof( ts ), ts,
					sizeof( uint64_t ), VK_QUERY_RESULT_64_BIT ) == VK_SUCCESS && ts[1] >= ts[0] ) {
				rttSum[ sct ] += (double)( ts[1] - ts[0] ) * rttPeriodNs * 1e-6;
			}
		}
		rttFrames++;
	}
	Com_Memset( rttState[ vk.cmd_index ], 0, sizeof( rttState[0] ) );
	if ( !r_rtTimings->integer ) {
		return;
	}
	qvkCmdResetQueryPool( vk.cmd->command_buffer, rttPool, base, RTT_COUNT * 2 );

	if ( rttFrames > 0 && ri.Milliseconds() - rttLastPrint >= 2000 ) {
		char line[512];
		line[0] = '\0';
		for ( sct = 0; sct < RTT_COUNT; sct++ ) {
			Q_strcat( line, sizeof( line ), va( "%s%s %.2f", sct ? ", " : "", rttNames[ sct ], rttSum[ sct ] / rttFrames ) );
		}
		ri.Printf( PRINT_ALL, "RT timings (GPU ms/frame over %i frames): %s\n", rttFrames, line );
		Com_Memset( rttSum, 0, sizeof( rttSum ) );
		rttFrames = 0;
		rttLastPrint = ri.Milliseconds();
	}
	vk_timing_begin( RTT_FRAME );
}

void vk_timing_begin( int section )
{
	if ( rttPool == VK_NULL_HANDLE || !r_rtTimings->integer || vk.cmd == NULL || rttState[ vk.cmd_index ][ section ] ) {
		return;
	}
	qvkCmdWriteTimestamp( vk.cmd->command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, rttPool,
		( vk.cmd_index * RTT_COUNT + section ) * 2 );
	rttState[ vk.cmd_index ][ section ] = 1;
}

void vk_timing_end( int section )
{
	if ( rttPool == VK_NULL_HANDLE || !r_rtTimings->integer || vk.cmd == NULL || rttState[ vk.cmd_index ][ section ] != 1 ) {
		return;
	}
	qvkCmdWriteTimestamp( vk.cmd->command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, rttPool,
		( vk.cmd_index * RTT_COUNT + section ) * 2 + 1 );
	rttState[ vk.cmd_index ][ section ] = 3;
}
