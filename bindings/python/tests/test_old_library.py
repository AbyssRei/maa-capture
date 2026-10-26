"""New Python binding against a pre-extension ABI 2 library (explicit path)."""
import sys
from maa_capture import Manager,CustomTarget,ScreencapCallbacks,provider_frame,set_lib_path,runtime_info,CapturePolicy,method_behavior

set_lib_path(sys.argv[1])
assert runtime_info().features & 24 == 0, "Pass the pre-extension ABI 2 library"
with Manager(target=CustomTarget("test")) as manager:
    assert manager.register_custom_unit("test",ScreencapCallbacks(lambda:provider_frame(source_key="test",data=b"123",width=1,height=1,channels=3)))
    assert manager.connect()
    frame=manager.screencap()
    assert frame.image.data==b"123" and frame.sample.flags==0 and frame.sample.sampled_at_ns==0
try:
    Manager(target=CustomTarget("test"),framepool_cache=True)
except RuntimeError as error:
    assert "does not support FramePool caching" in str(error)
else:
    raise AssertionError("Old library silently accepted a new policy")
print("Pre-extension ABI 2: default path works, metadata unknown, cache policy rejected")
for call in (lambda:Manager(target=CustomTarget("test"),policy=CapturePolicy(capture_timeout_ns=1)),lambda:method_behavior(1)):
    try: call()
    except RuntimeError: pass
    else: raise AssertionError("Old library silently accepted an advanced feature")
with Manager(target=CustomTarget("test")) as manager:
    try: manager.cancellation_handle()
    except RuntimeError: pass
    else: raise AssertionError("Old library silently accepted cancellation")
