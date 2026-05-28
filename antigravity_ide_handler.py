# -*- coding: utf-8 -*-
import sys
import os
import subprocess
import urllib.parse
import traceback

LOG_FILE = r"c:\PRojects\ESPRojects\snmp_agent_eth01\handler_log.txt"

def log(msg):
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(msg + "\n")
    except Exception:
        pass

def main():
    log("=== Handler Started ===")
    log(f"sys.argv: {sys.argv}")
    
    if len(sys.argv) < 2:
        log("Error: No arguments passed to handler")
        return
        
    url = sys.argv[1]
    log(f"Received URL: {url}")
    
    parsed = urllib.parse.urlparse(url)
    log(f"Parsed URL scheme: {parsed.scheme}, netloc: {parsed.netloc}, path: {parsed.path}, query: {parsed.query}")
    
    path = ""
    query_params = urllib.parse.parse_qs(parsed.query)
    if "path" in query_params:
        path = query_params["path"][0]
        log(f"Extracted path from query: {path}")
    
    if not path:
        # Handle urls like antigravity-ide://file/c%3A/PRojects/...
        # netloc = "file", path = "/c%3A/PRojects/..."
        # We strip leading slashes and decode it.
        full_url_path = parsed.path
        if parsed.netloc and parsed.netloc != "file":
            # Just in case netloc is part of the path
            full_url_path = parsed.netloc + "/" + full_url_path.lstrip("/")
            
        full_url_path = full_url_path.lstrip("/")
        path = urllib.parse.unquote(full_url_path)
        log(f"Decoded path from netloc/path: {path}")
            
    if not path:
        # Fallback to simple replacement
        cleaned = url.replace("antigravity-ide://", "").replace("antigravity://", "")
        if cleaned.startswith("open?path="):
            cleaned = cleaned[len("open?path="):]
        path = urllib.parse.unquote(cleaned)
        log(f"Fallback cleaned/unquoted path: {path}")
        
    path = os.path.abspath(path.strip())
    log(f"Resolved absolute path: {path}")
    
    if not os.path.exists(path):
        log(f"Error: Resolved path does not exist: {path}")
        # Default back to current directory of the handler script
        fallback_path = os.path.dirname(os.path.abspath(__file__))
        log(f"Using fallback directory of the handler: {fallback_path}")
        path = fallback_path

    log(f"Attempting to launch VS Code on: {path}")
    
    # 1. Try launching 'code' from path
    try:
        log("Trying launch via 'code' in shell...")
        subprocess.run(["code", path], shell=True, check=True)
        log("Launch via 'code' command was successful")
        return
    except Exception as e:
        log(f"Launch via 'code' command failed: {e}")
        
    # 2. Try explicit executable paths
    local_app_data = os.environ.get("LOCALAPPDATA", "")
    program_files = os.environ.get("ProgramFiles", "")
    paths_to_try = [
        os.path.join(local_app_data, "Programs", "Microsoft VS Code", "bin", "code.cmd"),
        os.path.join(local_app_data, "Programs", "Microsoft VS Code", "Code.exe"),
        os.path.join(program_files, "Microsoft VS Code", "bin", "code.cmd"),
        os.path.join(program_files, "Microsoft VS Code", "Code.exe"),
    ]
    
    log(f"Explicit paths to try: {paths_to_try}")
    
    launched = False
    for p in paths_to_try:
        log(f"Checking path: {p}")
        if os.path.exists(p):
            log(f"Path exists! Attempting execution: {p}")
            try:
                subprocess.run([p, path], shell=True, check=True)
                log(f"Successfully launched via explicit path: {p}")
                launched = True
                break
            except Exception as e:
                log(f"Failed to execute {p}: {e}")
                
    if not launched:
        log("Error: Could not launch VS Code through any of the methods.")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        log(f"Unhandled exception in handler main: {e}")
        log(traceback.format_exc())
