# client.py - Cliente del servicio de mensajería
# Sistemas Distribuidos - UC3M - Parte 1
#
# Cliente concurrente multihilo: un hilo para la interfaz de usuario
# y otro para recibir mensajes del servidor (creado en CONNECT).

from enum import Enum
import argparse
import socket
import threading
import struct
import time

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

    # ==================== HILO DE ESCUCHA (receptor de mensajes) ====================

    @staticmethod
    def _listen_thread_func(port, stop_event):
        """
        Hilo que escucha en 'port' los mensajes enviados por el servidor.
        Se crea en CONNECT y se destruye en DISCONNECT.
        """
        listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            listen_sock.bind(('', port))
            listen_sock.listen(10)
            listen_sock.settimeout(1.0)  # timeout para comprobar stop_event

            while not stop_event.is_set():
                try:
                    conn, _ = listen_sock.accept()
                    # Cada mensaje se gestiona en su propio hilo
                    t = threading.Thread(
                        target=client._handle_server_message,
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
    def _handle_server_message(conn):
        """
        Gestiona un mensaje recibido del servidor en el hilo de escucha.
        Tipos de mensaje (sección 8.6):
          - SEND_MESSAGE   : mensaje de otro usuario
          - SEND_MESS_ACK  : confirmación de entrega de un mensaje enviado
        """
        try:
            op = client._recv_string(conn)

            if op == "SEND_MESSAGE":
                # El servidor envía: sender\\0 id\\0 message\\0
                sender  = client._recv_string(conn)
                msg_id  = client._recv_string(conn)
                message = client._recv_string(conn)
                # Mostrar el mensaje según sección 6.7
                print(f"\ns> MESSAGE {msg_id} FROM {sender}")
                print(f"   {message}")
                print(f"   END")

            elif op == "SEND_MESS_ACK":
                # El servidor notifica que el mensaje fue entregado
                msg_id = client._recv_string(conn)
                print(f"\nc> SEND MESSAGE {msg_id} OK")

            # Parte 2 (ficheros adjuntos): SEND_MESSAGE_ATTACH, SEND_MESS_ATTACH_ACK
            # No implementado en Parte 1

        except Exception:
            pass
        finally:
            conn.close()

    # ==================== OPERACIONES DEL PROTOCOLO ====================

    @staticmethod
    def register(user):
        """
        Registra un usuario en el servidor (sección 6.2 y 8.1).
        Devuelve RC.OK, RC.USER_ERROR o RC.ERROR.
        """
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
        """
        Da de baja a un usuario del servidor (sección 6.3 y 8.2).
        Devuelve RC.OK, RC.USER_ERROR o RC.ERROR.
        """
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
        """
        Conecta al usuario al servicio de mensajería (sección 6.4 y 8.3).
        Proceso:
          1. Busca un puerto libre.
          2. Crea el hilo de escucha en ese puerto.
          3. Envía la solicitud de conexión al servidor.
        Devuelve RC.OK, RC.USER_ERROR o RC.ERROR.
        """
        stop_event = threading.Event()
        listen_thread = None

        try:
            # 1. Buscar puerto libre
            port = client._find_free_port()

            # 2. Crear hilo de escucha ANTES de enviar la solicitud
            stop_event = threading.Event()
            listen_thread = threading.Thread(
                target=client._listen_thread_func,
                args=(port, stop_event),
                daemon=True
            )
            listen_thread.start()

            # Pequeña espera para que el socket de escucha esté listo
            time.sleep(0.1)

            # 3. Enviar solicitud de conexión al servidor
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_string(s, "CONNECT")
            client._send_string(s, user)
            client._send_string(s, str(port))
            result = client._recv_byte(s)
            s.close()

            if result == 0:
                # Guardar estado de conexión
                client._connected_user = user
                client._listen_port    = port
                client._listen_thread  = listen_thread
                client._stop_event     = stop_event
                print("c> CONNECT OK")
                return client.RC.OK

            # En caso de error, detener el hilo de escucha
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
            if stop_event is not None:
                stop_event.set()
            print("c> CONNECT FAIL")
            return client.RC.ERROR

    @staticmethod
    def disconnect(user):
        """
        Desconecta al usuario del servicio (sección 6.5 y 8.4).
        El hilo de escucha se detiene independientemente del resultado.
        Devuelve RC.OK, RC.USER_ERROR o RC.ERROR.
        """
        # Parar el hilo de escucha siempre (sección 6.5)
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
    def users():
        """
        Solicita la lista de usuarios conectados (sección 6.8 y 8.7).
        Requiere que el cliente esté conectado (_connected_user != None).
        Devuelve RC.OK, RC.USER_ERROR o RC.ERROR.
        """
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_string(s, "USERS")
            # Enviar nombre del usuario que hace la petición
            username = client._connected_user if client._connected_user else ""
            client._send_string(s, username)
            result = client._recv_byte(s)

            if result == 0:
                count_str  = client._recv_string(s)
                count      = int(count_str)
                users_list = []
                for _ in range(count):
                    u = client._recv_string(s)
                    users_list.append(u)
                s.close()
                print(f"c> CONNECTED USERS ({count} users connected) OK")
                for u in users_list:
                    print(f"   {u}")
                return client.RC.OK

            elif result == 1:
                s.close()
                print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
                return client.RC.USER_ERROR
            else:
                s.close()
                print("c> CONNECTED USERS FAIL")
                return client.RC.ERROR

        except Exception:
            print("c> CONNECTED USERS FAIL")
            return client.RC.ERROR

    @staticmethod
    def send(user, message):
        """
        Envía un mensaje de texto a otro usuario (sección 6.6 y 8.5).
        El remitente es _connected_user. 'user' es el destinatario.
        Devuelve RC.OK, RC.USER_ERROR o RC.ERROR.
        """
        try:
            # Limitar mensaje a 255 caracteres (sección 8.5)
            msg = message[:255]

            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_string(s, "SEND")
            sender = client._connected_user if client._connected_user else ""
            client._send_string(s, sender)   # remitente
            client._send_string(s, user)     # destinatario
            client._send_string(s, msg)      # mensaje
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
        """
        Envía un mensaje con fichero adjunto (Parte 2 - no implementado aquí).
        """
        # Implementar en Parte 2
        print("c> SENDATTACH FAIL")
        return client.RC.ERROR

    # ==================== INTERFAZ DE USUARIO (SHELL) ====================

    @staticmethod
    def shell():
        """Bucle principal de la interfaz de usuario."""
        while True:
            try:
                command = input("c> ")
                line    = command.split(" ")
                if len(line) == 0:
                    continue

                line[0] = line[0].upper()

                if line[0] == "REGISTER":
                    if len(line) == 2:
                        client.register(line[1])
                    else:
                        print("Sintaxis: REGISTER <userName>")

                elif line[0] == "UNREGISTER":
                    if len(line) == 2:
                        client.unregister(line[1])
                    else:
                        print("Sintaxis: UNREGISTER <userName>")

                elif line[0] == "CONNECT":
                    if len(line) == 2:
                        client.connect(line[1])
                    else:
                        print("Sintaxis: CONNECT <userName>")

                elif line[0] == "DISCONNECT":
                    if len(line) == 2:
                        client.disconnect(line[1])
                    else:
                        print("Sintaxis: DISCONNECT <userName>")

                elif line[0] == "USERS":
                    if len(line) == 1:
                        client.users()
                    else:
                        print("Sintaxis: USERS")

                elif line[0] == "SEND":
                    if len(line) >= 3:
                        message = ' '.join(line[2:])
                        client.send(line[1], message)
                    else:
                        print("Sintaxis: SEND <userName> <message>")

                elif line[0] == "SENDATTACH":
                    if len(line) >= 4:
                        message = ' '.join(line[3:])
                        client.sendAttach(line[1], line[2], message)
                    else:
                        print("Sintaxis: SENDATTACH <userName> <filename> <message>")

                elif line[0] == "QUIT":
                    if len(line) == 1:
                        break
                    else:
                        print("Sintaxis: QUIT")

                else:
                    print(f"Error: comando '{line[0]}' no válido.")

            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Excepción: {e}")

    @staticmethod
    def usage():
        print("Uso: python3 client.py -s <servidor> -p <puerto>")

    @staticmethod
    def parseArguments(argv):
        """Parsea los argumentos de la línea de comandos."""
        parser = argparse.ArgumentParser(
            description='Cliente del servicio de mensajería SSDD'
        )
        parser.add_argument('-s', type=str, required=True, help='IP del servidor')
        parser.add_argument('-p', type=int, required=True, help='Puerto del servidor')
        args = parser.parse_args()

        if args.s is None:
            parser.error("Falta la IP del servidor")
            return False

        if args.p < 1024 or args.p > 65535:
            parser.error("El puerto debe estar en el rango [1024, 65535]")
            return False

        # IMPORTANTE: asignar a variables de clase (no locales)
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


# ==================== PUNTO DE ENTRADA ====================

if __name__ == "__main__":
    import sys
    client.main(sys.argv)