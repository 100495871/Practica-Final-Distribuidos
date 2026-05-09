import socket
from socket_utils import SocketUtils
from protocol import ProtocolCommands, RC

class ServerConnection:
    def __init__(self, server_ip, server_port):
        self.server_ip = server_ip
        self.server_port = server_port
    
    def _connect(self):
        """Crea y retorna un socket conectado al servidor"""
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((self.server_ip, self.server_port))
        return s
    
    def register(self, user):
        """Registra un usuario"""
        try:
            s = self._connect()
            SocketUtils.send_string(s, ProtocolCommands.REGISTER)
            SocketUtils.send_string(s, user)
            result = SocketUtils.recv_byte(s)
            s.close()
            return result
        except Exception:
            return 1  # ERROR
    
    def unregister(self, user):
        """Elimina un usuario"""
        try:
            s = self._connect()
            SocketUtils.send_string(s, ProtocolCommands.UNREGISTER)
            SocketUtils.send_string(s, user)
            result = SocketUtils.recv_byte(s)
            s.close()
            return result
        except Exception:
            return 1
    
    def connect(self, user, listen_port):
        """Conecta un usuario"""
        try:
            s = self._connect()
            SocketUtils.send_string(s, ProtocolCommands.CONNECT)
            SocketUtils.send_string(s, user)
            SocketUtils.send_string(s, str(listen_port))
            result = SocketUtils.recv_byte(s)
            s.close()
            return result
        except Exception:
            return 1
    
    def disconnect(self, current_user):
        """Desconecta un usuario"""
        try:
            s = self._connect()
            SocketUtils.send_string(s, ProtocolCommands.DISCONNECT)
            SocketUtils.send_string(s, current_user)
            result = SocketUtils.recv_byte(s)
            s.close()
            return result
        except Exception:
            return 1
    
    def get_users(self, current_user=""):
        """Obtiene lista de usuarios conectados"""
        try:
            s = self._connect()
            SocketUtils.send_string(s, ProtocolCommands.USERS)
            SocketUtils.send_string(s, current_user)
            result = SocketUtils.recv_byte(s)
            
            users = {}
            if result == 0:
                count = int(SocketUtils.recv_string(s))
                for _ in range(count):
                    info = SocketUtils.recv_string(s)
                    parts = info.split(" :: ")
                    if len(parts) == 3:
                        users[parts[0]] = (parts[1], int(parts[2]))
            s.close()
            return result, users
        except Exception:
            return 1, {}
    
    def send_message(self, sender, recipient, message):
        """Envía un mensaje simple"""
        try:
            s = self._connect()
            SocketUtils.send_string(s, ProtocolCommands.SEND)
            SocketUtils.send_string(s, sender)
            SocketUtils.send_string(s, recipient)
            SocketUtils.send_string(s, message[:255])
            result = SocketUtils.recv_byte(s)
            
            msg_id = None
            if result == 0:
                msg_id = SocketUtils.recv_string(s)
            s.close()
            return result, msg_id
        except Exception:
            return 1, None
    
    def send_attach(self, sender, recipient, message, filename):
        """Envía un mensaje con adjunto"""
        try:
            s = self._connect()
            SocketUtils.send_string(s, ProtocolCommands.SENDATTACH)
            SocketUtils.send_string(s, sender)
            SocketUtils.send_string(s, recipient)
            SocketUtils.send_string(s, message[:255])
            SocketUtils.send_string(s, filename)
            result = SocketUtils.recv_byte(s)
            
            msg_id = None
            if result == 0:
                msg_id = SocketUtils.recv_string(s)
            s.close()
            return result, msg_id
        except Exception:
            return 1, None