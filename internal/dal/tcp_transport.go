package dal

import "net"

// TCPTransport is a transport via TCP for vendotek devices
// ALWAYS KEEP IN MIND TCP actually opens 2 channels! So write and read are NOT synced
type TCPTransport struct {
	Address string
	Port    string
	Conn    net.Conn
}

// NewTCPTransport is a constructor for the transport layer
func NewTCPTransport(newAddress, newPort string) (VTKTransport, error) {
	res := &TCPTransport{
		Address: newAddress,
		Port:    newPort,
	}
	var err error
	res.Conn, err = net.Dial("tcp", res.Address+":"+res.Port)
	if err != nil {
		return nil, err
	}
	return res, nil
}

// WriteBytes is to write the bytes to TCP
func (t *TCPTransport) WriteBytes(bytes ...byte) (int, error) {
	return t.Conn.Write(bytes)
}

// ReadBytes is to read the bytes from TCP
func (t *TCPTransport) ReadBytes(bytes []byte) (int, error) {
	return t.Conn.Read(bytes)
}
