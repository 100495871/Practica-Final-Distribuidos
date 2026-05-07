from enum import Enum
import argparse
import socket
import threading
import struct
import time
import os
import requests

class client:

    # ==================== TIPOS ====================

    class RC(Enum):
        OK         = 0
        ERROR      = 1
        USER_ERROR = 2

    # ==================== ATRIBUTOS DE CLASE ====================

    _server         = None   # IP del servidor
    _port           = -1     # Puerto del servidor
    _connected_user = None   # Usuario actualmente conectado
    _listen_port    = None   # Puerto de escucha de mensajes
    _listen_thread  = None   # Hilo de escucha
    _stop_event     = None   # Event para detener el hilo de escucha
    
    # Estructura para almacenar usuarios conectados: {username: (ip, port)}
    _connected_users_list = {}

    # ==================== FUNCIONES AUXILIARES DE PROTOCOLO ====================

    @staticmethod
    def _send_string(sock, s):
        """Envía una cadena terminada en \\0 por el socket."""
        data = s.encode('utf-8') + b'\x00'
        sock.sendall(data)

    @staticmethod
    def _recv_string(sock):
        """Recibe una cadena terminada en \\0 del socket. Devuelve str."""
        result = b''
        while True:
            ch = sock.recv(1)
            if not ch or ch == b'\x00':
                break
            result += ch
        return result.decode('utf-8')

    @staticmethod
    def _recv_byte(sock):
        """Recibe un único byte del socket. Devuelve int (0-255)."""
        data = sock.recv(1)
        if not data:
            raise ConnectionError("Conexión cerrada inesperadamente")
        return struct.unpack('B', data)[0]

    @staticmethod
    def _find_free_port():
        """Busca un puerto libre en el sistema."""
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(('', 0))
            return s.getsockname()[1]

    # ==================== SERVICIO WEB (NORMALIZADOR) ====================

    @staticmethod
    def _normalize_message(message):
        """Llama al servicio web local para normalizar el mensaje."""
        try:
            # Por simplicidad, se asume que el servicio web corre en localhost:5000
            response = requests.get(f"http://localhost:5000/normalize", params={"text": message}, timeout=2)
            if response.status_code == 200:
                return response.json().get("result", message)
        except Exception:
            pass
        # Si falla el servicio web, se devuelve el mensaje original (comportamiento robusto)
        return message

    # ==================== HILO DE ESCUCHA (receptor de mensajes) ====================

    @staticmethod
    def _listen_thread_func(port, stop_event):
        """
        Hilo que escucha en 'port' los mensajes enviados por el servidor o por otros clientes.
        """
        listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            listen_sock.bind(('', port))
            listen_sock.listen(10)
            listen_sock.settimeout(1.0)

            while not stop_event.is_set():
                try:
                    conn, _ = listen_sock.accept()
                    t = threading.Thread(
                        target=client._handle_incoming_connection,
                        args=(conn,),
                        daemon=True
                    )
                    t.start()
                except socket.timeout:
                    continue
                except OSError:
                    break
        finally:
            listen_sock.close()

    @staticmethod
    def _handle_incoming_connection(conn):
        """Gestiona una conexión entrante (del servidor o de otro cliente)."""
        try:
            op = client._recv_string(conn)

            if op == "SEND_MESSAGE":
                sender  = client._recv_string(conn)
                msg_id  = client._recv_string(conn)
                message = client._recv_string(conn)
                print(f"\ns> MESSAGE {msg_id} FROM {sender}")
                print(f"   {message}")
                print(f"   END")

            elif op == "SEND_MESS_ACK":
                msg_id = client._recv_string(conn)
                print(f"\nc> SEND MESSAGE {msg_id} OK")

            elif op == "SEND_MESSAGE_ATTACH":
                sender   = client._recv_string(conn)
                msg_id   = client._recv_string(conn)
                message  = client._recv_string(conn)
                filename = client._recv_string(conn)
                print(f"\ns> MESSAGE {msg_id} FROM {sender}")
                print(f"   {message}")
                print(f"   END")
                print(f"   FILE {filename}")

            elif op == "SEND_MESS_ATTACH_ACK":
                msg_id   = client._recv_string(conn)
                filename = client._recv_string(conn)
                print(f"\nc> SEND MESSAGE {msg_id} {filename} OK")
                
            elif op == "GET_FILE":
                # Transferencia directa cliente-cliente (Parte 2, Secc 2.5)
                requester = client._recv_string(conn)
                filename  = client._recv_string(conn)
                
                if os.path.exists(filename):
                    with open(filename, 'rb') as f:
                        content = f.read()
                        conn.sendall(content)
                else:
                    # Si no existe, cerramos la conexión o mandamos algo vacío
                    pass

        except Exception:
            pass
        finally:
            conn.close()

    # ==================== OPERACIONES DEL PROTOCOLO ====================

    @staticmethod
    def register(user):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_string(s, "REGISTER")
            client._send_string(s, user)
            result = client._recv_byte(s)
            s.close()

            if result == 0:
                print("c> REGISTER OK")
                return client.RC.OK
            elif result == 1:
                print("c> USERNAME IN USE")
                return client.RC.USER_ERROR
            else:
                print("c> REGISTER FAIL")
                return client.RC.ERROR
        except Exception:
            print("c> REGISTER FAIL")
            return client.RC.ERROR

    @staticmethod
    def unregister(user):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_string(s, "UNREGISTER")
            client._send_string(s, user)
            result = client._recv_byte(s)
            s.close()

            if result == 0:
                print("c> UNREGISTER OK")
                return client.RC.OK
            elif result == 1:
                print("c> USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                print("c> UNREGISTER FAIL")
                return client.RC.ERROR
        except Exception:
            print("c> UNREGISTER FAIL")
            return client.RC.ERROR

    @staticmethod
    def connect(user):
        try:
            port = client._find_free_port()
            stop_event = threading.Event()
            listen_thread = threading.Thread(
                target=client._listen_thread_func,
                args=(port, stop_event),
                daemon=True
            )
            listen_thread.start()
            time.sleep(0.1)

            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_string(s, "CONNECT")
            client._send_string(s, user)
            client._send_string(s, str(port))
            result = client._recv_byte(s)
            s.close()

            if result == 0:
                client._connected_user = user
                client._listen_port    = port
                client._listen_thread  = listen_thread
                client._stop_event     = stop_event
                print("c> CONNECT OK")
                # Al conectar, refrescar la lista de usuarios para obtener IPs/Puertos
                client.users(silent=True)
                return client.RC.OK

            stop_event.set()
            if result == 1:
                print("c> CONNECT FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            elif result == 2:
                print("c> USER ALREADY CONNECTED")
                return client.RC.USER_ERROR
            else:
                print("c> CONNECT FAIL")
                return client.RC.ERROR
        except Exception:
            print("c> CONNECT FAIL")
            return client.RC.ERROR

    @staticmethod
    def disconnect(user):
        def _stop_listen():
            if client._stop_event is not None:
                client._stop_event.set()
            client._connected_user = None
            client._listen_port    = None
            client._listen_thread  = None
            client._stop_event     = None

        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_string(s, "DISCONNECT")
            client._send_string(s, user)
            result = client._recv_byte(s)
            s.close()
            _stop_listen()
            if result == 0:
                print("c> DISCONNECT OK")
                return client.RC.OK
            elif result == 1:
                print("c> DISCONNECT FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            elif result == 2:
                print("c> DISCONNECT FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR
            else:
                print("c> DISCONNECT FAIL")
                return client.RC.ERROR
        except Exception:
            _stop_listen()
            print("c> DISCONNECT FAIL")
            return client.RC.ERROR

    @staticmethod
    def users(silent=False):
        """Solicita la lista de usuarios conectados con IP y puerto (Parte 2, Secc 2.4)."""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_string(s, "USERS")
            username = client._connected_user if client._connected_user else ""
            client._send_string(s, username)
            result = client._recv_byte(s)

            if result == 0:
                count_str  = client._recv_string(s)
                count      = int(count_str)
                client._connected_users_list = {}
                display_list = []
                for _ in range(count):
                    # Formato: usuario :: IP :: puerto
                    u_info = client._recv_string(s)
                    parts = u_info.split(" :: ")
                    if len(parts) == 3:
                        uname, ip, port = parts
                        client._connected_users_list[uname] = (ip, int(port))
                        display_list.append(uname)
                s.close()
                if not silent:
                    print(f"c> CONNECTED USERS ({count} users connected) OK")
                    for u in display_list:
                        print(f"   {u}")
                return client.RC.OK
            else:
                s.close()
                if not silent: print("c> CONNECTED USERS FAIL")
                return client.RC.ERROR
        except Exception:
            if not silent: print("c> CONNECTED USERS FAIL")
            return client.RC.ERROR

    @staticmethod
    def send(user, message):
        try:
            # Servicio Web: Normalizar mensaje (Parte 2, Secc 3)
            message = client._normalize_message(message)
            msg = message[:255]

            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_string(s, "SEND")
            sender = client._connected_user if client._connected_user else ""
            client._send_string(s, sender)
            client._send_string(s, user)
            client._send_string(s, msg)
            result = client._recv_byte(s)

            if result == 0:
                msg_id = client._recv_string(s)
                s.close()
                print(f"c> SEND OK - MESSAGE {msg_id}")
                return client.RC.OK
            elif result == 1:
                s.close()
                print("c> SEND FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                s.close()
                print("c> SEND FAIL")
                return client.RC.ERROR
        except Exception:
            print("c> SEND FAIL")
            return client.RC.ERROR

    @staticmethod
    def sendAttach(user, file, message):
        """Envía un mensaje con fichero adjunto (Parte 2, Secc 2.1)."""
        try:
            if not os.path.exists(file):
                print(f"c> SENDATTACH FAIL, FILE {file} DOES NOT EXIST")
                return client.RC.ERROR
            
            # Normalizar mensaje
            message = client._normalize_message(message)
            msg = message[:255]

            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_string(s, "SENDATTACH")
            sender = client._connected_user if client._connected_user else ""
            client._send_string(s, sender)
            client._send_string(s, user)
            client._send_string(s, msg)
            client._send_string(s, file)
            result = client._recv_byte(s)

            if result == 0:
                msg_id = client._recv_string(s)
                s.close()
                print(f"c> SENDATTACH OK - MESSAGE {msg_id}")
                return client.RC.OK
            elif result == 1:
                s.close()
                print("c> SENDATTACH FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                s.close()
                print("c> SENDATTACH FAIL")
                return client.RC.ERROR
        except Exception:
            print("c> SENDATTACH FAIL")
            return client.RC.ERROR
            
    @staticmethod
    def getFile(user, remote_file, local_file):
        """Solicitud de transferencia de ficheros directa entre clientes (Parte 2, Secc 2.5)."""
        try:
            # 1. Buscar en la estructura de datos local IP y Puerto del usuario origen
            if user not in client._connected_users_list:
                # Refrescar lista de usuarios conectados internamente
                client.users(silent=True)
            
            if user not in client._connected_users_list:
                print("c> FILE TRANSFER FAILED, user not connected.")
                return client.RC.USER_ERROR
            
            ip, port = client._connected_users_list[user]
            
            # 2. Conexión directa al hilo de escucha del cliente origen
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                s.connect((ip, port))
            except Exception:
                print("c> FILE TRANSFER FAILED, IP or port not reachable.")
                return client.RC.ERROR
            
            # 3. Protocolo directo
            client._send_string(s, "GET_FILE")
            client._send_string(s, client._connected_user)
            client._send_string(s, remote_file)
            
            # 4. Recibir contenido y guardar
            with open(local_file, 'wb') as f:
                while True:
                    data = s.recv(4096)
                    if not data:
                        break
                    f.write(data)
            s.close()
            print(f"c> GETFILE {remote_file} FROM {user} OK")
            return client.RC.OK
            
        except Exception:
            print("c> GETFILE FAIL")
            return client.RC.ERROR

    # ==================== INTERFAZ DE USUARIO (SHELL) ====================

    @staticmethod
    def shell():
        while True:
            try:
                command = input("c> ")
                line    = command.split(" ")
                if len(line) == 0 or line[0] == "":
                    continue

                cmd = line[0].upper()

                if cmd == "REGISTER":
                    if len(line) == 2: client.register(line[1])
                    else: print("Sintaxis: REGISTER <userName>")

                elif cmd == "UNREGISTER":
                    if len(line) == 2: client.unregister(line[1])
                    else: print("Sintaxis: UNREGISTER <userName>")

                elif cmd == "CONNECT":
                    if len(line) == 2: client.connect(line[1])
                    else: print("Sintaxis: CONNECT <userName>")

                elif cmd == "DISCONNECT":
                    if len(line) == 2: client.disconnect(line[1])
                    else: print("Sintaxis: DISCONNECT <userName>")

                elif cmd == "USERS":
                    if len(line) == 1: client.users()
                    else: print("Sintaxis: USERS")

                elif cmd == "SEND":
                    if len(line) >= 3:
                        message = ' '.join(line[2:])
                        client.send(line[1], message)
                    else: print("Sintaxis: SEND <userName> <message>")

                elif cmd == "SENDATTACH":
                    if len(line) >= 4:
                        # Formato: SENDATTACH <userName> <message> <fileName>
                        # Nota: El enunciado Parte 2 Secc 2.1 dice: SENDATTACH <userName> <message> <fileName>
                        # pero el Shell de Parte 1 decía <userName> <filename> <message>. 
                        # Seguimos Secc 2.1 de Parte 2.
                        user = line[1]
                        filename = line[-1]
                        message = ' '.join(line[2:-1])
                        client.sendAttach(user, filename, message)
                    else: print("Sintaxis: SENDATTACH <userName> <message> <fileName>")
                
                elif cmd == "GETFILE":
                    if len(line) == 4:
                        client.getFile(line[1], line[2], line[3])
                    else: print("Sintaxis: GETFILE <userName> <remoteFileName> <localFileName>")

                elif cmd == "QUIT":
                    break

                else:
                    print(f"Error: comando '{cmd}' no válido.")

            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Excepción: {e}")

    @staticmethod
    def usage():
        print("Uso: python3 client.py -s <servidor> -p <puerto>")

    @staticmethod
    def parseArguments(argv):
        parser = argparse.ArgumentParser(description='Cliente del servicio de mensajería SSDD')
        parser.add_argument('-s', type=str, required=True, help='IP del servidor')
        parser.add_argument('-p', type=int, required=True, help='Puerto del servidor')
        args = parser.parse_args()
        client._server = args.s
        client._port   = args.p
        return True

    @staticmethod
    def main(argv):
        if not client.parseArguments(argv):
            client.usage()
            return
        client.shell()
        print("+++ FINISHED +++")

if __name__ == "__main__":
    import sys
    client.main(sys.argv)
