from flask import *
import hashlib

app = Flask(__name__)

@app.route("/binary", methods=["GET"])
def send_binary():
    sketch_md5 = request.headers.get(["x-esp32-sketch-md5"], None)
    if not sketch_md5: abort(400)
    server_sketch_md5 = hashlib.md5(open("node_binary.bin", "rb").read()).hexdigest()

    

    return send_file("node_binary.bin")


app.run("0.0.0.0", debug=True)