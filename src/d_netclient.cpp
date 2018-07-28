//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1999-2016 Randy Heit
// Copyright 2002-2016 Christoph Oelckers
// Copyright 2018 Magnus Norddahl
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//

#include <stddef.h>
#include <inttypes.h>

#include "version.h"
#include "menu/menu.h"
#include "m_random.h"
#include "i_system.h"
#include "i_video.h"
#include "i_net.h"
#include "g_game.h"
#include "doomdef.h"
#include "doomstat.h"
#include "c_console.h"
#include "d_netinf.h"
#include "d_netclient.h"
#include "d_netsingle.h"
#include "cmdlib.h"
#include "s_sound.h"
#include "m_cheat.h"
#include "p_local.h"
#include "c_dispatch.h"
#include "sbar.h"
#include "gi.h"
#include "m_misc.h"
#include "gameconfigfile.h"
#include "d_gui.h"
#include "templates.h"
#include "p_acs.h"
#include "p_trace.h"
#include "a_sharedglobal.h"
#include "st_start.h"
#include "teaminfo.h"
#include "p_conversation.h"
#include "g_level.h"
#include "d_event.h"
#include "m_argv.h"
#include "p_lnspec.h"
#include "v_video.h"
#include "p_spec.h"
#include "hardware.h"
#include "r_utility.h"
#include "a_keys.h"
#include "intermission/intermission.h"
#include "g_levellocals.h"
#include "events.h"
#include "i_time.h"

NetClient::NetClient(FString server)
{
	Printf("Connecting to %s..\n", server.GetChars());

	mComm = I_InitNetwork(0);
	mServerNode = mComm->Connect(server);
	mStatus = NodeStatus::InPreGame;

	NetPacket packet;
	packet.node = mServerNode;

	NetCommand cmd ( NetPacketType::ConnectRequest );
	cmd.writeCommandToPacket ( packet );

	mComm->PacketSend(packet);
}

void NetClient::Update()
{
	while (true)
	{
		NetPacket packet;
		mComm->PacketGet(packet);
		if (packet.node == -1)
			break;

		if (packet.node != mServerNode)
		{
			mComm->Close(packet.node);
		}
		else if (packet.size == 0)
		{
			OnClose(packet);
			break;
		}
		else
		{
			NetPacketType type = (NetPacketType)packet.stream.ReadByte();
			switch (type)
			{
			default: OnClose(packet); break;
			case NetPacketType::ConnectResponse: OnConnectResponse(packet); break;
			case NetPacketType::Disconnect: OnDisconnect(packet); break;
			case NetPacketType::Tic: OnTic(packet); break;
			}
		}

		if (mStatus == NodeStatus::Closed)
		{
			if (network.get() == this)
			{
				network.reset(new NetSinglePlayer());
				G_EndNetGame();
			}
			else
			{
				netconnect.reset();
			}
			return;
		}
	}
}

void NetClient::SetCurrentTic(int receivetic, int sendtic)
{
	gametic = receivetic;
	mSendTic = sendtic;
}

void NetClient::EndCurrentTic()
{
	NetPacket packet;
	packet.node = mServerNode;

	NetCommand cmd ( NetPacketType::Tic );
	cmd.addByte ( 0 ); // target gametic
	cmd.addBuffer ( &mCurrentInput[consoleplayer].ucmd, sizeof(usercmd_t) );
	cmd.writeCommandToPacket ( packet );

	mComm->PacketSend(packet);

	mCurrentCommands = mSendCommands;
	mSendCommands.Clear();
}

int NetClient::GetSendTick() const
{
	return mSendTic;
}

ticcmd_t NetClient::GetPlayerInput(int player) const
{
	return mCurrentInput[player];
}

ticcmd_t NetClient::GetSentInput(int tic) const
{
	return mSentInput[tic % BACKUPTICS];
}

void NetClient::RunCommands(int player)
{
	if (player == consoleplayer)
	{
		Net_RunCommands(mCurrentCommands, consoleplayer);
	}
}

void NetClient::WriteLocalInput(ticcmd_t cmd)
{
	mCurrentInput[consoleplayer] = cmd;
	mSentInput[gametic % BACKUPTICS] = cmd;
}

void NetClient::WriteBotInput(int player, const ticcmd_t &cmd)
{
	mCurrentInput[player] = cmd;
}

void NetClient::WriteBytes(const uint8_t *block, int len)
{
	mSendCommands.AppendData(block, len);
}

int NetClient::GetPing(int player) const
{
	return 0;
}

int NetClient::GetServerPing() const
{
	return 0;
}

void NetClient::ListPingTimes()
{
}

void NetClient::Network_Controller(int playernum, bool add)
{
}

void NetClient::OnClose(const NetPacket &packet)
{
	mComm->Close(mServerNode);
	mServerNode = -1;
	mStatus = NodeStatus::Closed;

	if (network.get() == this)
	{
		Printf("Disconnected\n");
	}
	else
	{
		Printf("Could not connect\n");
	}
}

void NetClient::OnConnectResponse(NetPacket &packet)
{
	if (packet.size != 3)
		return;

	int version = packet.stream.ReadByte(); // Protocol version
	if (version == 1)
	{
		int playernum = packet.stream.ReadByte();
		if (playernum != 255) // Join accepted
		{
			mPlayer = playernum;
			mStatus = NodeStatus::InGame;

			G_InitClientNetGame(mPlayer, "e1m1");

			network = std::move(netconnect);
		}
		else // Server full
		{
			mComm->Close(mServerNode);
			mServerNode = -1;
			mStatus = NodeStatus::Closed;

			Printf("Could not connect: server is full!\n");
		}
	}
	else
	{
		Printf("Could not connect: version mismatch.\n");
		mComm->Close(mServerNode);
		mServerNode = -1;
		mStatus = NodeStatus::Closed;
	}
}

void NetClient::OnDisconnect(const NetPacket &packet)
{
	mComm->Close(packet.node);
	mServerNode = -1;
	mStatus = NodeStatus::Closed;
}

void NetClient::OnTic(NetPacket &packet)
{
	if (packet.size != 2 + sizeof(float) * 5)
		return;

	int tic = packet.stream.ReadByte();
	float x = packet.stream.ReadFloat();
	float y = packet.stream.ReadFloat();
	float z = packet.stream.ReadFloat();
	float yaw = packet.stream.ReadFloat();
	float pitch = packet.stream.ReadFloat();

	if (playeringame[consoleplayer] && players[consoleplayer].mo)
	{
		players[consoleplayer].mo->SetXYZ(x, y, z);
		players[consoleplayer].mo->Angles.Yaw = yaw;
		players[consoleplayer].mo->Angles.Pitch = pitch;
	}
}
