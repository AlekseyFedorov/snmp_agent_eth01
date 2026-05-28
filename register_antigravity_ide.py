import os
import sys
import winreg
import urllib.parse
import subprocess

SCRIPT_PATH = os.path.abspath(__file__)
DIR_PATH = os.path.dirname(SCRIPT_PATH)
HANDLER_PATH = os.path.join(DIR_PATH, "antigravity_ide_handler.py")

HANDLER_CODE = """# -*- coding: utf-8 -*-
import sys
import os
import subprocess
import urllib.parse

def main():
    if len(sys.argv) < 2:
        return
    url = sys.argv[1]
    
    parsed = urllib.parse.urlparse(url)
    
    path = ""
    query_params = urllib.parse.parse_qs(parsed.query)
    if "path" in query_params:
        path = query_params["path"][0]
    
    if not path:
        netloc = parsed.netloc
        parsed_path = parsed.path
        if netloc and parsed_path:
            path = netloc + parsed_path
        elif parsed_path:
            path = parsed_path
            
    if not path:
        cleaned = url.replace("antigravity-ide://", "").replace("antigravity://", "")
        if cleaned.startswith("open?path="):
            cleaned = cleaned[len("open?path="):]
        path = urllib.parse.unquote(cleaned)
        
    path = os.path.abspath(path.strip())
    
    if os.path.exists(path):
        try:
            subprocess.run(["code", path], shell=True, check=True)
            return
        except Exception:
            pass
            
        local_app_data = os.environ.get("LOCALAPPDATA", "")
        program_files = os.environ.get("ProgramFiles", "")
        paths_to_try = [
            os.path.join(local_app_data, "Programs", "Microsoft VS Code", "bin", "code.cmd"),
            os.path.join(local_app_data, "Programs", "Microsoft VS Code", "Code.exe"),
            os.path.join(program_files, "Microsoft VS Code", "bin", "code.cmd"),
            os.path.join(program_files, "Microsoft VS Code", "Code.exe"),
        ]
        for p in paths_to_try:
            if os.path.exists(p):
                subprocess.run([p, path], shell=True)
                return

if __name__ == "__main__":
    main()
"""

def register_protocol():
    with open(HANDLER_PATH, "w", encoding="utf-8") as f:
        f.write(HANDLER_CODE)
    print(f"[OK] Created handler at: {HANDLER_PATH}")

    python_exe = sys.executable
    command_str = f'"{python_exe}" "{HANDLER_PATH}" "%1"'

    protocols = ["antigravity-ide", "antigravity"]

    for protocol in protocols:
        try:
            key_path = f"Software\\\\Classes\\\\{protocol}"
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key_path) as key:
                winreg.SetValueEx(key, "", 0, winreg.REG_SZ, f"URL:{protocol.capitalize()} Protocol")
                winreg.SetValueEx(key, "URL Protocol", 0, winreg.REG_SZ, "")
                
            cmd_key_path = f"{key_path}\\\\shell\\\\open\\\\command"
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, cmd_key_path) as cmd_key:
                winreg.SetValueEx(cmd_key, "", 0, winreg.REG_SZ, command_str)
                
            print(f"[OK] Protocol {protocol}:// successfully registered in Windows Registry!")
        except Exception as e:
            print(f"[ERROR] Failed to register {protocol}://: {e}")

if __name__ == "__main__":
    register_protocol()
