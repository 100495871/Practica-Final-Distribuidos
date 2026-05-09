#!/usr/bin/env python3
import argparse
import os
from server_connection import ServerConnection
from message_receiver import MessageReceiver
from socket_utils import SocketUtils
from protocol import RC, ProtocolCommands

class ChatClient:
    def __init__(self, server_ip, server_port):
        self.connection = ServerConnection(server_ip, server_port)
        self.receiver = None
        self.current_user = None
        self.users_cache = {}
    
    def _print_success(self, message):
        print(f"c> {message}")
    
    def _print_error(self, message):
        print(f"c> {message}")
    
    def register(self, user):
        result = self.connection.register(user)
        if result == 0:
            self._print_success("REGISTRO EXITOSO")
            return RC.OK
        elif result == 1:
            self._print_error("USUARIO EN USO")
            return RC.USER_ERROR
        else:
            self._print_error("REGISTRO FALLIDO")
            return RC.ERROR
    
    def unregister(self, user):
        result = self.connection.unregister(user)
        if result == 0:
            self._print_success("CIERRE DE SESIÓN EXITOSO")
            return RC.OK
        elif result == 1:
            self._print_error(f"CIERRE DE SESIÓN FALLIDO, EL USUARIO {user} NO EXISTE")
            return RC.USER_ERROR
        else:
            self._print_error("CIERRE DE SESIÓN FALLIDO")
            return RC.ERROR
    
    def connect(self, user):
        port = SocketUtils.find_free_port()
        self.receiver = MessageReceiver(port)
        
        # Configurar callbacks
        self.receiver.on_message_callback = self._on_message_received
        self.receiver.on_ack_callback = self._on_ack_received
        self.receiver.on_file_request_callback = self._on_file_request
        
        stop_event = self.receiver.start()
        
        result = self.connection.connect(user, port)
        
        if result == 0:
            self.current_user = user
            self._print_success("CONNECT OK") # Refrescar lista
            return RC.OK
        
        self.receiver.stop()
        if result == 1:
            self._print_error("FALLO AL CONECTAR, EL USUARIO NO EXISTE")
            return RC.USER_ERROR
        elif result == 2:
            self._print_error("USUARIO YA CONECTADO")
            return RC.USER_ERROR
        else:
            self._print_error("FALLO AL CONECTAR")
            return RC.ERROR
    
    def disconnect(self, user):
        if not user==self.current_user:
            self._print_error(f"DESCONEXIÓN FALLIDA, NO ERES EL USUARIO{user}")
            return RC.USER_ERROR
        result = self.connection.disconnect(user)
        
        if self.receiver:
            self.receiver.stop()
            self.receiver = None
        
        self.current_user = None
        
        if result == 0:
            self._print_success("DESCONEXIÓN OK")
            return RC.OK
        elif result == 1:
            self._print_error("FALLO EN LA DESCONEXIÓN, EL USUARIO NO EXISTE")
            return RC.USER_ERROR
        elif result == 2:
            self._print_error("FALLO EN LA DESCONEXIÓN, EL USUARIO NO ESTA CONECTADO")
            return RC.USER_ERROR
        else:
            self._print_error("FALLO EN LA DESCONEXIÓN")
            return RC.ERROR
    
    def users(self):
        result, users = self.connection.get_users(self.current_user or "")
        
        if result == 0:
            self.users_cache = users
            print(f"c> USUARIOS CONECTADOS: ({len(users)} usuarios conectados) OK")
            for user in users.keys():
                print(f"   {user}")
            return RC.OK
        else:
            self._print_error("FALLO AL LISTAR USUARIOS CONECTADOS")
            return RC.ERROR
    
    def send(self, user, message):
        if not self.current_user:
            self._print_error("No conectado")
            return RC.ERROR
        
        message = self._normalize_message(message)
        
        result, msg_id = self.connection.send_message(self.current_user, user, message)
        
        if result == 0:
            self._print_success(f"SEND OK - MENSAJE {msg_id}")
            return RC.OK
        elif result == 1:
            self._print_error(f"FALLO AL ENVIAR, EL USUARIO {user} NO EXISTE")
            return RC.USER_ERROR
        else:
            self._print_error("FALLO AL ENVIAR")
            return RC.ERROR
    
    def send_attach(self, user, filename, message):
        if not self.current_user:
            self._print_error("No conectado")
            return RC.ERROR
        
        if not os.path.exists(filename):
            self._print_error(f"SENDATTACH FALLIDO, EL ARCHIVO {filename} NO EXISTE")
            return RC.ERROR
        
        result, msg_id = self.connection.send_attach(self.current_user, user, message, filename)
        
        if result == 0:
            self._print_success(f"SENDATTACH EXITOSO - MENSAJE {msg_id}")
            return RC.OK
        elif result == 1:
            self._print_error("SENDATTACH FALLIDO, EL USUARIO NO EXISTE")
            return RC.USER_ERROR
        else:
            self._print_error("SENDATTACH FALLIDO")
            return RC.ERROR
    
    def get_file(self, user, remote_file, local_file):
        if user not in self.users_cache:
            self.users()
        
        if user not in self.users_cache:
            self._print_error("TRANSFERENCIA DE ARCHIVO FALLIDA, el usuario no está conectado")
            return RC.USER_ERROR
        
        ip, port = self.users_cache[user]
        
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((ip, port))
            
            SocketUtils.send_string(s, "GET_FILE")
            SocketUtils.send_string(s, self.current_user)
            SocketUtils.send_string(s, remote_file)
            
            with open(local_file, 'wb') as f:
                while True:
                    data = s.recv(4096)
                    if not data:
                        break
                    f.write(data)
            s.close()
            
            self._print_success(f"GETFILE {remote_file} DE {user} OK")
            return RC.OK
        except Exception:
            self._print_error("GETFILE FALLIDO")
            return RC.ERROR
    
    def _on_message_received(self, sender, msg_id, message, filename=None):
        print(f"\ns> MESSAGE {msg_id} FROM {sender}")
        print(f"   {message}")
        if filename:
            print(f"   FILE {filename}")
        print("   END")
    
    def _on_ack_received(self, msg_id):
        print(f"\nc> SEND MESSAGE {msg_id} OK")
    
    def _on_file_request(self, conn, requester, filename):
        if os.path.exists(filename):
            with open(filename, 'rb') as f:
                conn.sendall(f.read())
    
    @staticmethod
    def _normalize_message(message):
        """Llama al servicio web local para normalizar el mensaje."""
        try:
            # Por simplicidad, se asume que el servicio web corre en localhost:5000
            response = requests.get(f"http://localhost:5000/normalize", json={"text": message}, timeout=2)
            if response.status_code == 200:
                return response.json().get("result", message)
            else:
                print(f"Error: {response.error}")
        except Exception as e:
            print(f"Excepción no esperada:{e}")
        # Si falla el servicio web, se devuelve el mensaje original (comportamiento robusto)
        return message
    
    def shell(self):
        """Interfaz de usuario interactiva"""
        commands = {
            ProtocolCommands.REGISTER: lambda args: self.register(args[1]) if len(args) == 2 else print("Sintaxis: REGISTER <userName>"),
            ProtocolCommands.UNREGISTER: lambda args: self.unregister(args[1]) if len(args) == 2 else print("Sintaxis: UNREGISTER <userName>"),
            ProtocolCommands.CONNECT: lambda args: self.connect(args[1]) if len(args) == 2 else print("Sintaxis: CONNECT <userName>"),
            ProtocolCommands.DISCONNECT: lambda args: self.disconnect(args[1]) if len(args) == 2 else print("Sintaxis: DISCONNECT <userName>"),
            ProtocolCommands.USERS: lambda args: self.users() if len(args) == 1 else print("Sintaxis: USERS"),
            ProtocolCommands.SEND: lambda args: self.send(args[1], ' '.join(args[2:])) if len(args) >= 3 else print("Sintaxis: SEND <userName> <message>"),
            ProtocolCommands.SENDATTACH: lambda args: self.send_attach(args[1], args[-1], ' '.join(args[2:-1])) if len(args) >= 4 else print("Sintaxis: SENDATTACH <userName> <message> <fileName>"),
            ProtocolCommands.GET_FILE: lambda args: self.get_file(args[1], args[2], args[3]) if len(args) == 4 else print("Sintaxis: GETFILE <userName> <remoteFileName> <localFileName>"),
            "QUIT": lambda args: None,
        }
        
        while True:
            try:
                command = input("c> ").strip()
                if not command:
                    continue
                
                parts = command.split()
                cmd = parts[0].upper()
                
                if cmd in ['QUIT']:
                    if self.current_user:
                        self.disconnect(self.current_user)
                    break
                
                if cmd in commands:
                    commands[cmd](parts)
                else:
                    print(f"Error: comando '{cmd}' no válido.")
            
            except KeyboardInterrupt:
                print("\n\nCerrando cliente...")
                if self.current_user:
                    self.disconnect(self.current_user)
                break
            except Exception as e:
                print(f"Error inesperado: {e}")

def main():
    parser = argparse.ArgumentParser(description='Cliente del servicio de mensajería SSDD')
    parser.add_argument('-s', type=str, required=True, help='IP del servidor')
    parser.add_argument('-p', type=int, required=True, help='Puerto del servidor')
    args = parser.parse_args()
    
    client = ChatClient(args.s, args.p)
    client.shell()

if __name__ == "__main__":
    main()