import socket
import struct

class SocketUtils:
    '''
    Esta clase nos sirve para encapsular las utilidades del socket de forma que el desarrollo se hace más modular
    y por tanto más fácil de mantener
    '''
    @staticmethod
    def send_string(sock, s):
        """Envía una cadena terminada en \0"""
        data = s.encode('utf-8') + b'\x00'
        sock.sendall(data)

    @staticmethod
    def recv_string(sock):
        """Recibe una cadena terminada en \0"""
        result = b''
        while True:
            ch = sock.recv(1)
            if not ch or ch == b'\x00':
                break
            result += ch
        return result.decode('utf-8')

    @staticmethod
    def recv_byte(sock):
        """Recibe un único byte"""
        data = sock.recv(1)
        if not data:
            raise ConnectionError("Conexión cerrada inesperadamente")
        return struct.unpack('B', data)[0]

    @staticmethod
    def find_free_port():
        """Busca un puerto libre"""
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(('', 0))
            return s.getsockname()[1]