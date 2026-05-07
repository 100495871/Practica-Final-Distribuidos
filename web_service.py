from flask import Flask, request, jsonify
import re

app = Flask(__name__)

@app.route('/normalize', methods=['GET'])
def normalize():
    text = request.args.get('text', '')
    # Eliminar espacios en blanco repetidos: separar por palabras y unir con un solo espacio
    normalized = " ".join(text.split())
    return jsonify({"result": normalized})

if __name__ == '__main__':
    # Se ejecuta en el puerto 5000 como se espera en client.py
    app.run(host='0.0.0.0', port=5000)