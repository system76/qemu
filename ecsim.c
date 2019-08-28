#define USE_EC_LLE 1

enum LleRequest {
    LLE_REQUEST_INIT = 0x00,
    LLE_REQUEST_READ = 0x01,
    LLE_REQUEST_WRITE = 0x02,
};

enum State {
    STATE_NONE,
    STATE_GET_ACPI,
    STATE_SET_ACPI,
    STATE_SET_ACPI_INDEX,
    STATE_GET_PROJECT,
    STATE_GET_VERSION,
    STATE_SPI_FOLLOW,
    STATE_SPI_COMMAND,
    STATE_SPI_READ_STATUS,
    STATE_SPI_READ_JEDEC,
    STATE_SPI_WRITE_STATUS,
    STATE_SPI_WRITE_STATUS_DATA,
    STATE_SPI_SECTOR_ERASE,
    STATE_SPI_SECTOR_ERASE_ADDRESS,
    STATE_SPI_HS_READ,
    STATE_SPI_HS_READ_ADDRESS,
    STATE_SPI_HS_READ_DATA,
    STATE_SPI_AAI_PROGRAM,
};

struct Ec {
    uint8_t superio_addr_port;
    uint8_t superio_addr;
    uint8_t superio_data_port;
    uint8_t superio_data[256];

    uint8_t data_port;
    uint8_t data;
    uint8_t cmd_port;
    uint8_t cmd;

    enum State state;
    uint8_t acpi_space[256];
    uint8_t acpi_space_i;
    uint8_t project[16];
    uint8_t project_i;
    uint8_t version[16];
    uint8_t version_i;

    uint8_t spi_sts;
    uint8_t spi_addr[4];
    uint8_t spi_addr_i;
    uint8_t spi_jedec[4];
    uint8_t spi_jedec_i;

    int lle_socket;
};

static int ec_lle_request(struct Ec * ec, enum LleRequest lle_request, uint8_t addr, uint8_t * value) {
    assert(ec != NULL);
    assert(value != NULL);

    if (ec->lle_socket >= 0) {
        uint8_t request[3] = { (uint8_t)lle_request, addr, *value };
        if (send(ec->lle_socket, request, sizeof(request), 0) >= (int)sizeof(request)) {
            uint8_t response[1] = { 0 };
            if (recv(ec->lle_socket, response, sizeof(response), 0) >= (int)sizeof(response)) {
                *value = response[0];

                // printf(
                //     "ec_lle_request(0x%02X, 0x%02X, 0x%02X) = 0x%02X\n",
                //     request[0],
                //     request[1],
                //     request[2],
                //     response[0]
                // );

                return 1;
            } else {
                perror("ec_lle_io: recv");
                close(ec->lle_socket);
                ec->lle_socket = -1;
            }
        } else {
            perror("ec_lle_io: send");
            close(ec->lle_socket);
            ec->lle_socket = -1;
        }
    }

    return -1;
}

static struct Ec ec_new(void) {
    struct Ec ec = {
        .superio_addr_port = 0x2e,
        .superio_addr = 0x00,
        .superio_data_port = 0x2f,
        .superio_data = {0},

        .data_port = 0x62,
        .data = 0,
        .cmd_port = 0x66,
        .cmd = 0,

        .state = STATE_NONE,
        .acpi_space = {0},
        .acpi_space_i = 0,
        .project = "N130ZU",
        .project_i = 0,
        .version = "07.02",
        .version_i = 0,

        .spi_sts = 0x1C,
        .spi_addr = {0x00, 0x00, 0x00, 0x00},
        .spi_addr_i = 0,
        .spi_jedec = {0xFF, 0xFF, 0xFE, 0xFF},
        .spi_jedec_i = 0,

        .lle_socket = -1,
    };

    // Set EC ID to 8587
    ec.superio_data[0x20] = 0x85;
    ec.superio_data[0x21] = 0x87;

    // Set ADP (0x01) and BAT0 (0x04)
    ec.acpi_space[0x10] = 0x05;

    // Set size to 128K
    ec.acpi_space[0xE5] = 0x80;

    // Attempt to connect to low-level emulation socket
    #if USE_EC_LLE
        int lle_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (lle_socket >= 0) {
            struct sockaddr_in addr;
            bzero(&addr, sizeof(addr));
            addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            addr.sin_port = htons(8587);
            addr.sin_family = AF_INET;
            if (connect(lle_socket, (struct sockaddr *)&addr, sizeof(addr)) >= 0) {
                ec.lle_socket = lle_socket;

                uint8_t val = 0;
                ec_lle_request(&ec, LLE_REQUEST_INIT, 0, &val);
            } else {
                perror("connect");
                close(lle_socket);
            }
        } else {
            perror("socket");
        }
    #endif

    if (ec.lle_socket >= 0) {
        printf("EC using low-level emulation\n");
    } else {
        printf("EC using high-level emulation\n");
    }

    return ec;
}

// static void ec_drop(struct Ec * ec) {
//     if (ec->lle_socket >= 0) {
//         close(ec->lle_socket);
//         ec->lle_socket = -1;
//     }
// }

static void ec_acpi_cmd(struct Ec * ec) {
    assert(ec != NULL);

    uint8_t cmd = ec->acpi_space[0xF8];
    uint8_t data1 = ec->acpi_space[0xF9];
    uint8_t data2 = ec->acpi_space[0xFA];
    uint8_t data3 = ec->acpi_space[0xFB];
    uint8_t data4 = ec->acpi_space[0xFC];
    uint8_t data5 = ec->acpi_space[0xFD];

    printf(
        " (acpi command 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X)",
        cmd, data1, data2, data3, data4, data5
    );

    switch (cmd) {
    case 0xB1:
        if (data3 == 0) {
            uint16_t addr = (((uint16_t)data1) << 8) | ((uint16_t)data2);
            data1 = 0;
            switch(addr) {
            case 0x2002:
                data1 = 0x06;
                break;
            }
            printf(" (read address 0x%04X = 0x%02X)", addr, data1);
        } else {
            printf(" (read address unsupported 0x%02X)", data3);
        }
        break;
    }

    ec->acpi_space[0xF9] = data1;
    ec->acpi_space[0xFA] = data2;
    ec->acpi_space[0xFB] = data3;
    ec->acpi_space[0xFC] = data4;
    ec->acpi_space[0xFD] = data5;
}

static uint8_t ec_io_read(struct Ec * ec, uint8_t addr) {
    assert(ec != NULL);

    uint8_t val = 0;
    if (ec_lle_request(ec, LLE_REQUEST_READ, addr, &val) >= 0) {
        return val;
    }

    switch (ec->state) {
    case STATE_GET_PROJECT:
        if ((ec->cmd & 1) == 0) {
            ec->data = ec->project[ec->project_i++];
            if (ec->data == 0) {
                ec->data = '$';
                ec->state = STATE_NONE;
            }
            ec->cmd |= 1;
        }
        break;
    case STATE_GET_VERSION:
        if ((ec->cmd & 1) == 0) {
            ec->data = ec->version[ec->version_i++];
            if (ec->data == 0) {
                ec->data = '$';
                ec->state = STATE_NONE;
            }
            ec->cmd |= 1;
        }
        break;
    default:
        break;
    }

    if (addr == ec->superio_addr_port) {
        val = ec->superio_addr;
        printf("read superio addr 0x%02X\n", val);
    } else if (addr == ec->superio_data_port) {
        val = ec->superio_data[ec->superio_addr];
        printf("read superio data 0x%02X = 0x%02X\n", ec->superio_addr, val);
    } else if (addr == ec->data_port) {
        val = ec->data;
        ec->data = 0;
        ec->cmd &= ~1;
        printf("read data:     0x%02X\n", val);
    } else if (addr == ec->cmd_port) {
        val = ec->cmd;
        // printf("read cmd:     0x%02X\n", val);
    } else {
        printf("read unsupported 0x%02X\n", addr);
    }

    return val;
}

static void ec_io_write(struct Ec * ec, uint8_t addr, uint8_t val) {
    assert(ec != NULL);

    if (ec_lle_request(ec, LLE_REQUEST_WRITE, addr, &val) >= 0) {
        return;
    }

    if (addr == ec->superio_addr_port) {
        ec->superio_addr = val;
        printf("write superio addr 0x%02X\n", val);
    } else if (addr == ec->superio_data_port) {
        ec->superio_data[ec->superio_addr] = val;
        printf("write superio data 0x%02X = 0x%02X\n", ec->superio_addr, val);
    } else if (addr == ec->data_port) {
        printf("write data:    0x%02X", val);

        switch (ec->state) {
        case STATE_GET_ACPI:
            ec->state = STATE_NONE;
            ec->data = ec->acpi_space[val];
            ec->cmd |= 1;
            printf(" (get acpi space 0x%02X = 0x%02X)", val, ec->data);
            break;
        case STATE_SET_ACPI:
            ec->state = STATE_SET_ACPI_INDEX;
            ec->acpi_space_i = val;
            printf(" (set acpi space 0x%02X)", val);
            break;
        case STATE_SET_ACPI_INDEX:
            ec->state = STATE_NONE;
            ec->acpi_space[ec->acpi_space_i] = val;
            printf(" (set acpi space 0x%02X = 0x%02X)", ec->acpi_space_i, val);
            if (ec->acpi_space_i == 0xF8) {
                ec_acpi_cmd(ec);
            }
            break;
        default:
            printf(" (unsupported)");
            break;
        }

        printf("\n");
    } else if (addr == ec->cmd_port) {
        printf("write command: 0x%02X", val);

        switch (ec->state) {
        case STATE_SPI_FOLLOW:
        case STATE_SPI_READ_STATUS:
        case STATE_SPI_READ_JEDEC:
        case STATE_SPI_WRITE_STATUS:
        case STATE_SPI_SECTOR_ERASE:
        case STATE_SPI_HS_READ:
        case STATE_SPI_HS_READ_DATA:
            switch(val) {
            case 0x01:
                ec->state = STATE_SPI_FOLLOW;
                printf(" (spi follow)");
                break;
            case 0x02:
                ec->state = STATE_SPI_COMMAND;
                printf(" (spi command)");
                break;
            case 0x03:
                printf(" (spi write)");
                switch (ec->state) {
                case STATE_SPI_WRITE_STATUS:
                    ec->state = STATE_SPI_WRITE_STATUS_DATA;
                    printf(" (write status)");
                    break;
                case STATE_SPI_SECTOR_ERASE:
                    ec->state = STATE_SPI_SECTOR_ERASE_ADDRESS;
                    printf(" (sector erase)");
                    break;
                case STATE_SPI_HS_READ:
                    ec->state = STATE_SPI_HS_READ_ADDRESS;
                    printf(" (high speed read)");
                    break;
                default:
                    printf(" (unsupported)");
                    break;
                }
                break;
            case 0x04:
                printf(" (spi read)");
                switch (ec->state) {
                case STATE_SPI_READ_STATUS:
                    ec->data = ec->spi_sts;
                    ec->cmd |= 1;
                    printf(" (read status)");
                    break;
                case STATE_SPI_READ_JEDEC:
                    if (ec->spi_jedec_i < sizeof(ec->spi_jedec)) {
                        ec->data = ec->spi_jedec[ec->spi_jedec_i++];
                        ec->cmd |= 1;
                    }
                    printf(" (read jedec)");
                    break;
                case STATE_SPI_HS_READ_DATA:
                    ec->data = 0xFF;
                    ec->cmd |= 1;
                    printf(" (high speed read 0x%08X)", 0);
                    break;
                default:
                    printf(" (unsupported)");
                    break;
                }
                break;
            case 0x05:
                ec->state = STATE_NONE;
                printf(" (spi unfollow)");
                break;
            default:
                printf(" (unsupported in spi follow)");
                break;
            }
            break;
        case STATE_SPI_WRITE_STATUS_DATA:
            ec->state = STATE_SPI_WRITE_STATUS;
            printf(" (spi write status 0x%02X)", val);
            ec->spi_sts = val; //TODO: Do not allow overwrite of read only flags
            break;
        case STATE_SPI_SECTOR_ERASE_ADDRESS:
            ec->state = STATE_SPI_SECTOR_ERASE;
            if (ec->spi_addr_i > 0) {
                ec->spi_addr[--ec->spi_addr_i] = val;
                printf(" (spi sector erase %i = 0x%02X)", ec->spi_addr_i, val);
            }
            if (ec->spi_addr_i == 0) {
                uint32_t address =
                    (uint32_t)ec->spi_addr[0] |
                    ((uint32_t)ec->spi_addr[1] << 8) |
                    ((uint32_t)ec->spi_addr[2] << 16) |
                    ((uint32_t)ec->spi_addr[3] << 24);
                printf(" (spi sector erase 0x%08X)", address);
                ec->state = STATE_SPI_FOLLOW;
            }
            break;
        case STATE_SPI_HS_READ_ADDRESS:
            if (ec->spi_addr_i == 0) {
                ec->state = STATE_SPI_HS_READ_DATA;
                printf(" (spi high speed read dummy)");
            } else {
                ec->state = STATE_SPI_HS_READ;
                if (ec->spi_addr_i > 0) {
                    ec->spi_addr[--ec->spi_addr_i] = val;
                    printf(" (spi high speed read %i = 0x%02X)", ec->spi_addr_i, val);
                }
                if (ec->spi_addr_i == 0) {
                    uint32_t address =
                        (uint32_t)ec->spi_addr[0] |
                        ((uint32_t)ec->spi_addr[1] << 8) |
                        ((uint32_t)ec->spi_addr[2] << 16) |
                        ((uint32_t)ec->spi_addr[3] << 24);
                    printf(" (spi high speed read 0x%08X)", address);
                }
            }
            break;
        case STATE_SPI_COMMAND:
            printf(" (spi command)");
            switch (val) {
            case 0x01:
                ec->state = STATE_SPI_WRITE_STATUS;
                printf(" (write status)");
                break;
            case 0x04:
                ec->state = STATE_SPI_FOLLOW;
                ec->spi_sts &= ~2;
                printf(" (write disable)");
                break;
            case 0x05:
                ec->state = STATE_SPI_READ_STATUS;
                printf(" (read status)");
                break;
            case 0x06:
                ec->state = STATE_SPI_FOLLOW;
                ec->spi_sts |= 2;
                printf(" (write enable)");
                break;
            case 0x0B:
                ec->state = STATE_SPI_HS_READ;
                ec->spi_addr[0] = 0;
                ec->spi_addr[1] = 0;
                ec->spi_addr[2] = 0;
                ec->spi_addr[3] = 0;
                ec->spi_addr_i = 3;
                printf(" (high speed read)");
                break;
            case 0x20:
            case 0xD7:
                ec->state = STATE_SPI_SECTOR_ERASE;
                ec->spi_addr[0] = 0;
                ec->spi_addr[1] = 0;
                ec->spi_addr[2] = 0;
                ec->spi_addr[3] = 0;
                ec->spi_addr_i = 3;
                printf(" (sector erase)");
                break;
            case 0x9F:
                ec->state = STATE_SPI_READ_JEDEC;
                ec->spi_jedec_i = 0;
                printf(" (read jedec)");
                break;
            case 0xAD:
                ec->state = STATE_SPI_AAI_PROGRAM;
                printf(" (aai program)");
                break;
            default:
                printf(" (unsupported)");
                break;
            }
            break;
            switch (val) {
            case 0x04:
                ec->data = ec->spi_sts;
                ec->cmd |= 1;
                break;
            default:
                printf(" (unsupported in spi command)");
                break;
            }
            break;
        default:
            switch(val) {
            case 0x01:
                ec->state = STATE_SPI_FOLLOW;
                printf(" (spi follow)");
                break;
            case 0x80:
                ec->state = STATE_GET_ACPI;
                printf(" (get acpi space)");
                break;
            case 0x81:
                ec->state = STATE_SET_ACPI;
                printf(" (set acpi space)");
                break;
            case 0x92:
                ec->state = STATE_GET_PROJECT;
                ec->project_i = 0;
                printf(" (get project)");
                break;
            case 0x93:
                ec->state = STATE_GET_VERSION;
                ec->version_i = 0;
                printf(" (get version)");
                break;
            case 0xDC:
                //TODO: Figure out what this does
                ec->state = STATE_NONE;
                ec->data = 0x33;
                ec->cmd |= 1;
                printf(" (0xDC)");
                break;
            case 0xDE:
                //TODO: Figure out what this does
                ec->state = STATE_NONE;
                printf(" (0xDE)");
                break;
            case 0xF0:
                //TODO: Figure out what this does
                ec->state = STATE_NONE;
                printf(" (0xF0)");
                break;
            default:
                printf(" (unsupported)");
                break;
            }
            break;
        }

        printf("\n");
    } else {
        printf("write unsupported 0x%02X: 0x%02X\n", addr, val);
    }
}

// Global EC functions

static struct Ec * EC_GLOBAL = NULL;

static struct Ec * ec_global(void) {
    if (EC_GLOBAL == NULL) {
        struct Ec * ec_ptr = calloc(1, sizeof(struct Ec));
        assert(ec_ptr != NULL);
        *ec_ptr = ec_new();
        EC_GLOBAL = ec_ptr;
    }
    return EC_GLOBAL;
}

static uint8_t ec_global_io_read(uint8_t addr) {
    return ec_io_read(ec_global(), addr);
}

static void ec_global_io_write(uint8_t addr, uint8_t val) {
    ec_io_write(ec_global(), addr, val);
}
