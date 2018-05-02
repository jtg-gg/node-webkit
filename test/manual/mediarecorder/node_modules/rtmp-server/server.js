const net = require('net');
const Client = require('./client');

const SESSION_ID_CHARS = 'abcdefghijklmnopqrstuvwxyz0123456789';
const SESSION_ID_LENGTH = 12;

class NMServer extends net.Server {
  constructor(opts) {
    super(opts);

    this.conns = {};
    this.producers = {};

    this.on('connection', socket => {
      const id = this.generateNewSessionID();

      const client = new Client(id, socket, this.conns, this.producers);
      client.on('error', err => socket.destroy(err));

      socket.on('data', data => client.bp.push(data));
      socket.on('end', () => client.stop());
      socket.on('error', err => client.emit('error', err));

      this.emit('client', client);
      client.run();
    });
  }

  generateNewSessionID() {
    let sessionId;

    do {
      sessionId = '';
      for (let i = 0; i < SESSION_ID_LENGTH; i++) {
        const charIndex = (Math.random() * SESSION_ID_CHARS.length) | 0;
        sessionId += SESSION_ID_CHARS.charAt(charIndex);
      }
    } while (this.conns.hasOwnProperty(sessionId));

    return sessionId;
  }
}

module.exports = NMServer;
