from flask import *
from flask import abort as fabort


import hashlib
import sqlite3
import re
from time import time


def abort(status_code, message):
    response = make_response(f'{status_code}\n{message}')
    response.status_code = status_code
    fabort(response)


def make_app():
    app = Flask(__name__)
    app.config["db_path"] = "./sqlite.db"
    app.config["secret_key"] = "super_secret_master_key"
    app.config["mac_pattern"] = re.compile("^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$")
    app.config["node_binary_path"] = "./node_binary"

    db = sqlite3.connect(app.config["db_path"])
    db.execute("""
        CREATE TABLE IF NOT EXISTS "scans" (
            "node_mac"	    TEXT,
            "device_mac"    TEXT,
            "strength"	    INTEGER,
            "time"	        TEXT,
        
        CONSTRAINT fk_node_mac 
            FOREIGN KEY (node_mac)
            REFERENCES nodes(node_mac)
        );
    """)
    db.commit()


    db.execute("""
        CREATE TABLE IF NOT EXISTS "nodes" (
            "node_mac"  TEXT UNIQUE,
            "coord_x"   FLOAT,
            "coord_y"	FLOAT,
            "coord_z"	FLOAT,
            PRIMARY KEY("node_mac")
        );
    """)


    return app

app = make_app()




def dict_factory(cursor, row):
    d = {}
    for idx, col in enumerate(cursor.description):
        d[col[0]] = row[idx]
    return d

def get_db():
    if 'db' not in g:
        g.db = sqlite3.connect(app.config["db_path"])
        g.db.row_factory = dict_factory
        g.db.execute("PRAGMA foreign_keys = ON")
    
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


def get_local_binary_md5():
    node_binary = open("node_binary.bin", "rb").read()
    return hashlib.md5(node_binary).hexdigest()
    



@app.route("/binary", methods=["GET"])
def send_binary():
    node_binary_md5 = request.headers.get(["x-esp32-sketch-md5"], None)
    if not node_binary_md5: abort(400, "missing header 'x-esp32-sketch-md5'")
    
    local_binary_md5 = get_local_binary_md5()
    if node_binary_md5 == local_binary_md5: abort(304, "binary md5 is same")

    return send_file("node_binary.bin")


@app.route("/scans/new", methods=["POST"])
def new_scan():
    scan_data = parse_scan_data()

    db = get_db()

    try: 
        for device in scan_data["found_devices"]:
            db.execute("""
                INSERT INTO scans
                (node_mac, device_mac, strength, time)
                VALUES
                (?, ?, ?, ?)    
            """, (scan_data["node_mac"], device["device_mac"], device["strength"], time()+int(device["time"])))
    except sqlite3.IntegrityError as e:
        abort(406, e)

    db.commit()
    

    return ""


@app.route("/time", methods=["GET"])
def send_time():
    return str(round(time()))



app.run("0.0.0.0", port=5000, debug=True)
