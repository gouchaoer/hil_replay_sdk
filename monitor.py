#coding=utf-8
from datetime import datetime, timedelta
import sys
import os
import json
import argparse
import pathlib
import glob
import os
from rich.console import Console
from rich.table import Table
from rich.live import Live
from rich.text import Text
import time
import random
import re
import math


def parse_apps():
    ENABLE_SERVICES=os.environ['ENABLE_SERVICES']
    APPS = re.split(r'\W+', ENABLE_SERVICES)
    APPS=[APP for APP in APPS if APP.strip() != '']
    APPS_REST = APPS.copy()
    paths=glob.glob("/dev/shm/MONITOR__*")
    apps={}

    def safe_remove_file(path):
        try:
            os.remove(path)
        except FileNotFoundError:
            pass
        except PermissionError:
            pass
        except OSError:
            pass

    for path in paths:
        try:
            file_name = os.path.basename(path)
            parts = file_name.split("__")
            app_name = parts[1]
            status = parts[2] 
            for APP in APPS:
                if app_name.startswith(APP):
                    f=open(path)
                    content=f.read()
                    f.close()
                    content = content.strip()
                    pattern = r'^\[(\d{1,20}.\d{3})\]' # startswith monotonic: [12345.678]
                    match = re.search(pattern, content)
                    if match:
                        monotonic_str = match.group(1)
                    else:
                        safe_remove_file(path) # abnornal
                        continue
                    monotonic_sec = float(monotonic_str)
                    now_monotonic_sec = time.monotonic()
                    if math.fabs(monotonic_sec - now_monotonic_sec) > 2: # 2s outdate
                        safe_remove_file(path)
                        continue
                    if app_name not in apps:
                        apps[app_name]= {}
                    apps[app_name][status] = content
                    if APP in APPS_REST:
                        APPS_REST.remove(APP)
                    continue   
        except Exception as e:
            print(f"{str(e)}")
    for APP in APPS_REST:
        apps[APP]= {}
        apps[APP]['ERROR'] = f"lost"
    return apps

def create_table(apps):

    now_str=datetime.now().strftime("%Y-%m-%d %H:%M:%S")  
    table = Table(title=f"MONITOR - {now_str}")
    table.add_column("SERVICES", justify="right")
    table.add_column("STATUS(2s)")
    table.add_column(f"[CLOCK_MONOTONIC][CLOCK_REALTIME][HIL_TS={os.environ['HIL_TS_SRC']}] MESSAGE")

    keys_li = list(apps.keys())
    keys_li.sort()
    for key in keys_li:
        app = apps[key]
        # a lucky bug here, [camera_0_7|OKAY] eaten by rich
        if 'ERROR' in app:
            table.add_row(key, Text("ERROR", style="red"), Text(app['ERROR'], style="red"))
        elif 'WARN' in app:
            table.add_row(key, Text("WARN", style="yellow"), Text(app['WARN'], style="yellow"))
        elif 'OKAY' in app:
            table.add_row(key, Text("OKAY", style="green"), Text(app['OKAY'], style="green"))
    return table


if len(sys.argv)>=2 and sys.argv[1]=='query':
    apps = parse_apps()
    console = Console()
    table = create_table(apps)
    console.print(table)

    f=open("monitor.log", "a", encoding="utf-8")
    console_f = Console(file=f)
    console_f.print(table)
    f.close()

    okay_ct=0
    warn_ct=0
    error_ct=0
    for key in apps:
        app = apps[key]
        if 'ERROR' in app:
            error_ct+=1
        elif 'WARN' in app:
            warn_ct+=1
        elif 'OKAY' in app:
            okay_ct+=1
    if error_ct > 0:
        sys.exit(2)
    if warn_ct > 0:
        sys.exit(1)
    sys.exit(0)

else:
    f=open("monitor.log", "a", encoding="utf-8")
    console_f = Console(file=f)
    with Live(create_table(parse_apps()), auto_refresh=False) as live:
        while True:
            try:
                time.sleep(1)
                table = create_table(parse_apps())
                console_f.print(table) # log into monitor.log
                live.update(table,refresh=True)
            except KeyboardInterrupt:
                break
            except Exception as e:
                raise e
    f.close()