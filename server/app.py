from flask import *
import hashlib
import sqlite3
import re
from time import time

def make_app():
    app = Flask(__name__)
    app.config["db_path"] = "./sqlite.db"
    app.config["secret_key"] = "super_secret_master_key"
    app.config["mac_pattern"] = re.compile("^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$")

    db = sqlite3.connect(app.config["db_path"])
    db.execute("""
        CREATE TABLE IF NOT EXISTS "scans" (
            "node_mac"	    STRING,
            "device_mac"    STRING,
            "strength"	    INTEGER,
            "time"	        STRING,
            "server_minute" STRING
        );
    """)
    db.commit()

    return app

app = make_app()




def dict_factory(cursor, row):
    d = {}
    for idx, col in enumerate(cursor.description):
        d[col[0]] = row[idx]
    return d

def get_db():
    app.config["db_path"]
    if 'db' not in g:
        g.db = sqlite3.connect(app.config["db_path"])
        g.db.row_factory = dict_factory
    

    return g.db



def parse_scan_data():

    request_data = request.get_json()

    found_devices = request_data.get("found_devices", [])
    node_mac = request_data.get("node_mac", "")
    secret_key = request_data.get("secret_key", "")


    if not type(found_devices) == list: abort(406, "type of found devices must be list")
    if not app.config["mac_pattern"].match(node_mac): abort(400, "node_mac does not match pattern")
    if not secret_key == app.config["secret_key"]: abort(401, "secret_key does not match")


    data = {
        "found_devices": found_devices,
        "node_mac": node_mac,
        "secret_key": secret_key
    }

    return data



@app.route("/binary", methods=["GET"])
def send_binary():
    sketch_md5 = request.headers.get(["x-esp32-sketch-md5"], None)
    if not sketch_md5: abort(400)
    server_sketch_md5 = hashlib.md5(open("node_binary.bin", "rb").read()).hexdigest()


    return send_file("node_binary.bin")


@app.route("/scans/new", methods=["POST"])
def new_scan():
    scan_data = parse_scan_data()

    db = get_db()

    for device in scan_data["found_devices"]:
        print(device)
        db.execute("""
            INSERT INTO scans
            (node_mac, device_mac, strength, time)
            VALUES
            (?, ?, ?, ?)    
        """, (scan_data["node_mac"], device["device_mac"], device["strength"], time()+int(device["time"])))
        db.commit()

    

    return ""



app.run("0.0.0.0", port=5000, debug=True)
