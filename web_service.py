from flask import Flask, request, jsonify

app = Flask(__name__)

@app.route('/normalize', methods=['GET'])
def normalize():
    try:
        # Obtenemos el texto del parámetro 'text' en la URL (?text=...)
        text = request.args.get('text')
        
        if text is None:
            return jsonify({"error": "Petición mal formada, falta el parámetro 'text'"}), 400

        # Normalización: split() sin argumentos separa por cualquier espacio en blanco 
        # y elimina los repetidos, join() los une con un solo espacio.
        normalized = " ".join(text.split())
        
        return jsonify({"result": normalized}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    # El servidor corre en el puerto 5000, que es donde el cliente lo busca
    app.run(host='0.0.0.0', port=5000)
