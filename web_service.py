from flask import Flask, request, jsonify
import re

app = Flask(__name__)

@app.route('/normalize', methods=['GET'])
def normalize():
    try:
        data = request.get_json()
        if not isinstance(data, dict):
            return jsonify({"error": "JSON inválido"}),400
        text = data.get("text")
        if text is None:
            return jsonify({"error": "Request mal formada, falta la etiqueta text"}), 400
        if not isinstance(text, str):
            text = str(text)

        # Eliminar espacios en blanco repetidos: separar por palabras y unir con un solo espacio
        normalized = " ".join(text.split())
        return jsonify({"result": normalized}), 200
    except KeyError:
        print("Error 400, el json enviado no es válido")
    except Exception as e:
        print(f"Error inesperado{e}")

if __name__ == '__main__':
    # Se ejecuta en el puerto 5000 como se espera en la parte cliente
    app.run(host='0.0.0.0', port=5000)