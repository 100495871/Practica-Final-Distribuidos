struct log_operation_args {
    string user<>;
    string operation<>;
};

struct log_sendattach_args {
    string user<>;
    string operation<>;
    string filename<>;
};

program LOGPROG {
    version LOGVERS {
        int log_operation(log_operation_args) = 1;
        int log_sendattach(log_sendattach_args) = 2;
    } = 1;
} = 0x20000001;