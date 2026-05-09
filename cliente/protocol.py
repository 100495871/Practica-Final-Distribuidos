from enum import Enum

class RC(Enum):
    OK         = 0
    ERROR      = 1
    USER_ERROR = 2

class ProtocolCommands:
    # Comandos del cliente al servidor
    REGISTER = "REGISTER"
    UNREGISTER = "UNREGISTER"
    CONNECT = "CONNECT"
    DISCONNECT = "DISCONNECT"
    USERS = "USERS"
    SEND = "SEND"
    SENDATTACH = "SENDATTACH"
    
    # Comandos del servidor al cliente
    SEND_MESSAGE = "SEND_MESSAGE"
    SEND_MESS_ACK = "SEND_MESS_ACK"
    SEND_MESSAGE_ATTACH = "SEND_MESSAGE_ATTACH"
    SEND_MESS_ATTACH_ACK = "SEND_MESS_ATTACH_ACK"
    GET_FILE = "GET_FILE"
