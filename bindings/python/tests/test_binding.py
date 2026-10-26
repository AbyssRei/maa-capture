"""Deterministic ABI 2 binding regression entry; desktop checks are explicit."""
from __future__ import annotations
import ctypes as C
from dataclasses import FrozenInstanceError, replace
import sys
import unittest
from unittest.mock import Mock
from maa_capture import *
from maa_capture import __version__
from maa_capture import _loader, _ffi as F
from maa_capture._types import to_native

class BindingTests(unittest.TestCase):
    def frame(self, value=10):
        return provider_frame(source_key="test",data=bytes([value,20,30,255,40,50,60,255,0,0,0,0]),width=2,height=1,step=12)
    def test_callback_frame_and_lifetime(self):
        calls = []
        value = [10]
        cb = ScreencapCallbacks(lambda:self.frame(value[0]),inactive=lambda:calls.append(1))
        manager = Manager(target=CustomTarget("test"))
        with manager:
            self.assertTrue(manager.register_custom_unit("test",cb))
            self.assertTrue(manager.connect(), manager.last_error())
            first = manager.screencap()
            value[0] = 99
            second = manager.screencap()
            self.assertEqual(first.image.data,bytes([10,20,30,40,50,60]))
            self.assertEqual(second.image.data[0],99)
            self.assertEqual(first.source.source_key,"test")
            self.assertEqual(first.source.verification,1)
            self.assertGreater(second.source.frame_id,first.source.frame_id)
            with self.assertRaises(FrozenInstanceError):
                first.source.source_key = "wrong"
        self.assertEqual(first.image.data[0],10)
        manager.close()
        self.assertEqual(calls,[1])
        with self.assertRaisesRegex(RuntimeError,"closed"):
            manager.screencap()
    def test_mismatch_never_publishes(self):
        with Manager(target=CustomTarget("test")) as m:
            cb = ScreencapCallbacks(lambda:provider_frame(source_key="other",data=b"123",width=1,height=1,channels=3))
            self.assertTrue(m.register_custom_unit("test",cb))
            self.assertFalse(m.connect())
            self.assertEqual(m.last_error().code,7)
            self.assertEqual(m.last_error(),m.last_error())
            self.assertIsNone(m.screencap())
            self.assertEqual(m.last_error().code,2)
    def test_callback_exception(self):
        def fail():
            raise ValueError("sentinel")
        with Manager(target=CustomTarget("test")) as m:
            self.assertTrue(m.register_custom_unit("test",ScreencapCallbacks(fail)))
            with self.assertLogs("maa_capture._core",level="ERROR"):
                self.assertFalse(m.connect())
    def test_invalid_provider_mapping(self):
        frame=self.frame()
        invalid=replace(frame,source=replace(frame.source,valid_fields=4,scale_x=float("nan"),scale_y=1.0))
        with Manager(target=CustomTarget("test")) as m:
            self.assertTrue(m.register_custom_unit("test",ScreencapCallbacks(lambda:invalid)))
            self.assertFalse(m.connect())
            self.assertEqual(m.last_error().code,12)
    def test_integer_and_target_validation(self):
        with self.assertRaises(ValueError):
            to_native(replace(self.frame().source,native_id=1<<64),F.MaaWSFrameInfo)
        with self.assertRaises(TypeError):
            Manager()
        for value in (-1,1<<64,True):
            with self.assertRaises((ValueError,TypeError)):
                Manager(target=WindowTarget(value,1))
        with self.assertRaises(ValueError):
            Manager(target=MonitorTarget("x"*512))
        with self.assertRaises(ValueError):
            Manager(target=CustomTarget("a\0b"))
        with self.assertRaises(CaptureException):
            Manager(target=CustomTarget("test"),methods=1)
        with self.assertRaises(CaptureException):
            method_capabilities(3)
        self.assertEqual(method_capabilities(32).scopes,2)
    def test_runtime_and_abi(self):
        info = runtime_info()
        self.assertEqual(info.abi_version,2)
        self.assertEqual(info.version,__version__)
        self.assertEqual(info.platform,{"win32":"windows","darwin":"macos","linux":"linux"}[sys.platform])
        self.assertEqual(C.sizeof(F.MaaWSCaptureTarget),816)
        self.assertEqual(C.sizeof(F.MaaWSImageView),40)
        missing = Mock(spec=[])
        with self.assertRaisesRegex(RuntimeError,rf"package={__version__}.*native=unknown.*unknown"):
            _loader._validate(missing,None)
        wrong = Mock()
        wrong.MaaWSGetAbiVersion.return_value = 1
        with self.assertRaisesRegex(RuntimeError,"expected ABI 2, got 1"):
            _loader._validate(wrong,"test.dll")
        wrong.MaaWSManagerCreate.assert_not_called()
        partial = Mock(spec=["MaaWSGetAbiVersion"])
        partial.MaaWSGetAbiVersion.return_value = 2
        with self.assertRaisesRegex(RuntimeError,"MaaWSGetRuntimeInfo"):
            _loader._validate(partial,"test.dll")
    def test_snapshots(self):
        if sys.platform not in ("win32","darwin"):
            with self.assertRaises(CaptureException) as result:
                monitors()
            self.assertEqual(result.exception.error.code,3)
            return
        try:
            snapshot = monitors()
        except CaptureException as error:
            self.skipTest(f"Desktop enumeration unavailable: {error}")
        self.assertIsInstance(snapshot.items,tuple)
        for item in snapshot.items:
            self.assertEqual(item.layout_token,snapshot.layout_token)
            self.assertTrue(item.monitor_key)
            self.assertGreater(item.pixel_width,0)
    def test_cleanup_on_python_exception(self):
        m = Manager(target=CustomTarget("test"))
        with self.assertRaisesRegex(ValueError,"sentinel"):
            with m:
                raise ValueError("sentinel")
        self.assertTrue(m.closed)

if __name__ == "__main__":
    if len(sys.argv)>1 and not sys.argv[1].startswith("-"):
        set_lib_path(sys.argv.pop(1))
    unittest.main()
