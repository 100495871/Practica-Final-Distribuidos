import socket
import threading
from socket_utils import SocketUtils
from protocol import ProtocolCommands

class MessageReceiver:
    '''
    Esta clase encapsula el funcionamiento del hilo receptor de mensajes 
    '''
    def __init__(self, port):
        self.port = port
        self.stop_event = threading.Event()
        self.thread = None
        self.on_message_callback = None
        self.on_ack_callback = None
        self.on_file_request_callback = None
    
    def start(self):
        """Inicia el hilo de escucha"""
        self.thread = threading.Thread(
            target=self._listen,
            daemon=True
        )
        self.thread.start()
        return self.stop_event
    
    def stop(self):
        """Detiene el hilo de escucha"""
        if self.stop_event:
            self.stop_event.set()
    
    def _listen(self):
        """Hilo principal de escucha"""
        listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            listen_sock.bind(('', self.port))
            listen_sock.listen(10)
            listen_sock.settimeout(1.0)
            
            while not self.stop_event.is_set():
                try:
                    conn, _ = listen_sock.accept()
                    t = threading.Thread(
                        target=self._handle_connection,
                        args=(conn,),
                        daemon=True
                    )
                    t.start()
                except socket.timeout:
                    continue
        finally:
            listen_sock.close()
    
    def _handle_connection(self, conn):
        """Maneja una conexión entrante"""
        try:
            op = SocketUtils.recv_string(conn)
            
            if op == ProtocolCommands.SEND_MESSAGE:
                sender = SocketUtils.recv_string(conn)
                msg_id = SocketUtils.recv_string(conn)
                message = SocketUtils.recv_string(conn)
                if self.on_message_callback:
                    self.on_message_callback(sender, msg_id, message)
            
            elif op == ProtocolCommands.SEND_MESSAGE_ATTACH:
                sender = SocketUtils.recv_string(conn)
                msg_id = SocketUtils.recv_string(conn)
                message = SocketUtils.recv_string(conn)
                filename = SocketUtils.recv_string(conn)
                if self.on_message_callback:
                    self.on_message_callback(sender, msg_id, message, filename)
            
            elif op == ProtocolCommands.SEND_MESS_ACK:
                msg_id = SocketUtils.recv_string(conn)
                if self.on_ack_callback:
                    self.on_ack_callback(msg_id)
            
            elif op == ProtocolCommands.GET_FILE:
                requester = SocketUtils.recv_string(conn)
                filename = SocketUtils.recv_string(conn)
                if self.on_file_request_callback:
                    self.on_file_request_callback(conn, requester, filename)
                    
        except Exception:
            pass
        finally:
            conn.close()