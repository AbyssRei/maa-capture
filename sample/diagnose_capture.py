"""Source/geometry diagnostics. Native capture runs in a disposable subprocess."""
from __future__ import annotations
import argparse
from dataclasses import asdict
import json
import multiprocessing as mp
import os
from pathlib import Path
import statistics
import sys
import time

def worker(args, connection):
    # Native logging cannot corrupt the parent's JSON output.
    with open(args.get("log_output") or os.devnull,"a",encoding="utf-8") as log:
        os.dup2(log.fileno(),1)
        os.dup2(log.fileno(),2)
        try:
            if "_pipewire_fd" in args:
                args["fd"] = args.pop("_pipewire_fd").detach()
            from maa_capture import (Manager,MonitorTarget,WindowTarget,CustomTarget,PipeWireTarget,WlrTarget,
                monitors,runtime_info,lib_path,set_lib_path,CaptureException)
            if args["lib"]:
                set_lib_path(args["lib"])
            result = {"runtime":asdict(runtime_info()),"loaded_path":lib_path()}
            if args["list_monitors"]:
                result["monitors"] = asdict(monitors())
                connection.send(result)
                return
            targets = {
                "monitor":lambda:MonitorTarget(args["key"]),
                "window":lambda:WindowTarget(args["window_id"],1 if sys.platform=="win32" else 2),
                "pipewire":lambda:PipeWireTarget(args["fd"],args["node_id"]),
                "wlr":lambda:WlrTarget(args["socket"],args["output_index"]),
            }
            target = targets[args["target"]]()
            result["requested"] = {"type":args["target"],**asdict(target)}
            with Manager(target=target,methods=args["method"]) as manager:
                start = time.perf_counter()
                connected = manager.connect()
                result["connect_ms"] = (time.perf_counter()-start)*1000
                result["connected"] = connected
                times,frames = [],[]
                if connected:
                    for _ in range(args["samples"]):
                        start = time.perf_counter()
                        frame = manager.screencap()
                        times.append((time.perf_counter()-start)*1000)
                        if frame is None:
                            result["error"] = asdict(manager.last_error())
                            break
                        frames.append(asdict(frame.source))
                        if args["image_output"]:
                            p = frame.image
                            rgb = bytearray()
                            for y in range(p.height):
                                row = p.data[y*p.step:y*p.step+p.width*3]
                                for x in range(0,len(row),3):
                                    rgb.extend(row[x:x+3][::-1])
                            Path(args["image_output"]).write_bytes(f"P6\n{p.width} {p.height}\n255\n".encode()+rgb)
                else:
                    result["error"] = asdict(manager.last_error())
                result["frames"] = frames
                if times:
                    ordered = sorted(times)
                    result["capture_ms"] = {"p50":statistics.median(times),"p95":ordered[min(len(ordered)-1,int(len(ordered)*0.95))]}
            connection.send(result)
        except Exception as error:
            payload = {"failure":str(error)}
            if hasattr(error,"error"):
                payload["error"] = asdict(error.error)
            connection.send(payload)
        finally:
            connection.close()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--lib")
    p.add_argument("--list-monitors",action="store_true")
    p.add_argument("--target",choices=("monitor","window","pipewire","wlr"),default="monitor")
    p.add_argument("--key",default="")
    p.add_argument("--window-id",type=lambda x:int(x,0),default=0)
    p.add_argument("--method",type=lambda x:int(x,0),default=0)
    p.add_argument("--samples",type=int,default=3)
    p.add_argument("--timeout",type=float,default=30)
    p.add_argument("--fd",type=int,default=-1)
    p.add_argument("--node-id",type=int,default=0)
    p.add_argument("--socket",default="")
    p.add_argument("--output-index",type=int,default=0)
    p.add_argument("--output",help="Optional JSON output path")
    p.add_argument("--image-output",help="Optional last frame PPM path")
    p.add_argument("--log-output",help="Optional native log path")
    a = p.parse_args()
    if a.samples <= 0 or a.timeout <= 0:
        p.error("samples and timeout must be positive")
    ctx = mp.get_context("spawn")
    values = vars(a).copy()
    if a.target == "pipewire" and a.fd >= 0 and sys.platform.startswith("linux"):
        from multiprocessing.reduction import DupFd
        values["_pipewire_fd"] = DupFd(a.fd)
    receive,send = ctx.Pipe(duplex=False)
    child = ctx.Process(target=worker,args=(values,send))
    child.start()
    send.close()
    try:
        if receive.poll(a.timeout):
            try:
                result = receive.recv()
            except EOFError:
                result = {"failure":"capture worker exited without a result","exitcode":child.exitcode}
        else:
            result = {"failure":"hard timeout","timeout_seconds":a.timeout}
    finally:
        if child.is_alive():
            child.terminate()
        child.join(5)
        if child.is_alive():
            child.kill()
            child.join()
        receive.close()
    text = json.dumps(result,ensure_ascii=False,indent=2)
    if a.output:
        Path(a.output).write_text(text,encoding="utf-8")
    print(text)
    return 1 if "failure" in result or "error" in result else 0

if __name__=="__main__":
    raise SystemExit(main())
