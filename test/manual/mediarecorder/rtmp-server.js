const RtmpServer = require('rtmp-server');
const rtmpServer = new RtmpServer();

rtmpServer.on('error', err => {
  process.send(err);
});

rtmpServer.on('client', client => {
  //client.on('command', command => {
  //  console.log(command.cmd, command);
  //});

  client.on('connect', () => {
     process.send('connect '+client.app);
  });
  
  client.on('play', ({ streamName }) => {
    process.send('PLAY '+streamName);
  });
  
  client.on('publish', ({ streamName }) => {
    process.send('PUBLISH '+streamName);
  });
  
  client.on('stop', () => {
    process.send('client disconnected');
  });
});

rtmpServer.listen(1935);
