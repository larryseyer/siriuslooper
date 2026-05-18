// =============================================================================
// CARemoteLayerRoundTripTests.mm — in-process CARemoteLayer sanity (M7 S6).
// =============================================================================
// Verifies Apple's CARemoteLayerServer / CARemoteLayerClient API works as
// documented BEFORE we depend on it being correct in the cross-process
// path. Constructs both ends in the same process; the API supports this
// (the engine talks to its own serverPort).
//
// Apple-only: tests/CMakeLists.txt gates the .mm on if(APPLE), so there
// is no non-Apple TU to compile.
//
// Tag: [plugin-editor-xpc][in-process]
//
// Note: CARemoteLayerServer is a process-global singleton; clients from
// prior TEST_CASEs in the same process may still be draining when the
// next case starts. Apple's docs don't specify the timing of
// `-[CARemoteLayerClient invalidate]`. If these tests flake, switch to
// a single TEST_CASE with SECTIONs to make ordering explicit.

#include <catch2/catch_test_macros.hpp>

#import <QuartzCore/QuartzCore.h>
#import <QuartzCore/CARemoteLayerServer.h>
#import <QuartzCore/CARemoteLayerClient.h>
#import <AppKit/AppKit.h>

TEST_CASE ("CARemoteLayerServer.sharedServer.serverPort is non-zero",
           "[plugin-editor-xpc][in-process]")
{
    @autoreleasepool {
        CARemoteLayerServer* server = [CARemoteLayerServer sharedServer];
        REQUIRE (server != nil);
        CHECK (server.serverPort != MACH_PORT_NULL);
    }
}

TEST_CASE ("CARemoteLayerClient initWithServerPort yields non-zero clientId",
           "[plugin-editor-xpc][in-process]")
{
    @autoreleasepool {
        CARemoteLayerServer* server = [CARemoteLayerServer sharedServer];
        REQUIRE (server != nil);

        CARemoteLayerClient* client = [[CARemoteLayerClient alloc]
            initWithServerPort: server.serverPort];
        REQUIRE (client != nil);
        CHECK (client.clientId != 0);

        CALayer* root = [CALayer layer];
        root.bounds = CGRectMake (0, 0, 200, 100);
        client.layer = root;
        CHECK (client.layer == root);

        [client invalidate];
        [client release];
    }
}

TEST_CASE ("+[CALayer layerWithRemoteClientId:] returns non-nil for live client",
           "[plugin-editor-xpc][in-process]")
{
    @autoreleasepool {
        CARemoteLayerServer* server = [CARemoteLayerServer sharedServer];
        REQUIRE (server != nil);

        CARemoteLayerClient* client = [[CARemoteLayerClient alloc]
            initWithServerPort: server.serverPort];
        REQUIRE (client != nil);

        CALayer* root = [CALayer layer];
        root.bounds = CGRectMake (0, 0, 200, 100);
        client.layer = root;

        CALayer* remoteProxy = [CALayer layerWithRemoteClientId: client.clientId];
        CHECK (remoteProxy != nil);

        [client invalidate];
        [client release];
    }
}
