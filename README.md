# meshchat

A decentralized chat network for cjdns, with an IRC front-end.

**Note**: this is experimental software. Use at your own risk!

## Usage

- Build and run: `make all && ./meshchat`
- Connect to `localhost:6999` in your IRC client.
- Join some channels.
- Wait for peers to be found.

## What meshchat does

- It discovers peers by pinging all the nodes in your local cjdns routing table.
- It makes connections between other peers running meshchat.
- It serves an ircd that you can connect to with your regular IRC client.

## What works

- You can use it and chat with people on the network.

## How it works

- **meshchat** finds potential peers by querying cjdns's routing table using
  your local cjdns admin port. It periodically sends a greeting to all such
  potential peers, containing your nick and list of channels.
- Each message you send from the IRC client is encapsulated into a UDP packet
  and sent over your cjdns interface to all the peers that your meshchat
  instance thinks are online and in the appropriate channel.
- When it receives a message from a peer, it relays it to your IRC client.

## What doesn't work

- Currently, the peer connections are over UDP. There is no re-sending or ACKs
  done, so messages may be dropped, especially if you sent many at once.
