process.env.HOST = '127.0.0.1';
process.env.PORT = '3737';

const { startService } = require('kugoumusicapi');

startService().catch((error) => {
  console.error('Failed to start the local Kugou API bridge:', error);
  process.exitCode = 1;
});
