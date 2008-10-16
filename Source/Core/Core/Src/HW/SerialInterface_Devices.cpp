// Copyright (C) 2003-2008 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <stdio.h>
#include <stdlib.h>

#include "SerialInterface_Devices.h"

#include "EXI_Device.h"
#include "EXI_DeviceMic.h"

#include "../Plugins/Plugin_PAD.h"

#include "../PowerPC/PowerPC.h"
#include "CPU.h"

#define SI_TYPE_GC              0x08000000u

#define SI_GC_STANDARD          0x01000000u // dolphin standard controller
#define SI_GC_NOMOTOR           0x20000000u // no rumble motor

#define SI_GC_KEYBOARD          (SI_TYPE_GC | 0x00200000)
#define SI_GC_CONTROLLER        (SI_TYPE_GC | SI_GC_STANDARD)

#define SI_MAX_COMCSR_INLNGTH   128
#define SI_MAX_COMCSR_OUTLNGTH  128

// =====================================================================================================
// --- base class ---
// =====================================================================================================

int ISIDevice::RunBuffer(u8* _pBuffer, int _iLength)
{
#ifdef _DEBUG
	LOG(SERIALINTERFACE, "Send Data Device(%i) - Length(%i)   ", ISIDevice::m_iDeviceNumber,_iLength);

	char szTemp[256] = "";
	int num = 0;
	while(num < _iLength)
	{		
		char szTemp2[128] = "";
		sprintf(szTemp2, "0x%02x ", _pBuffer[num^3]);
		strcat(szTemp, szTemp2);
		num++;

		if ((num % 8) == 0)
		{
			LOG(SERIALINTERFACE, szTemp);
			szTemp[0] = '\0';
		}		
	}
	LOG(SERIALINTERFACE, szTemp);		
#endif
	return 0;
};

// =====================================================================================================
// --- standard gamecube controller ---
// =====================================================================================================

CSIDevice_GCController::CSIDevice_GCController(int _iDeviceNumber) :
	ISIDevice(_iDeviceNumber)
{
	memset(&m_origin, 0, sizeof(SOrigin));

	m_origin.uCommand			= 0x41;
	m_origin.uOriginStickX		= 0x80;
	m_origin.uOriginStickY		= 0x80;
	m_origin.uSubStickStickX	= 0x80;
	m_origin.uSubStickStickY	= 0x80;
	m_origin.uTrigger_L			= 0x1F;
	m_origin.uTrigger_R			= 0x1F;
}

int CSIDevice_GCController::RunBuffer(u8* _pBuffer, int _iLength)
{
	// for debug logging only
	ISIDevice::RunBuffer(_pBuffer, _iLength);

	int iPosition = 0;
	while(iPosition < _iLength)
	{	
		// read the command
		EBufferCommands command = static_cast<EBufferCommands>(_pBuffer[iPosition ^ 3]);
		iPosition++;

		// handle it
		switch(command)
		{
		case CMD_RESET:
			{
				*(u32*)&_pBuffer[0] = SI_GC_CONTROLLER; // | SI_GC_NOMOTOR;
				iPosition = _iLength; // break the while loop
			}
			break;

		case CMD_ORIGIN:
			{
				LOG(SERIALINTERFACE, "SI - Get Origin");
				u8* pCalibration = reinterpret_cast<u8*>(&m_origin);
				for (int i = 0; i < (int)sizeof(SOrigin); i++)
				{
					_pBuffer[i ^ 3] = *pCalibration++;
				}				
			}
			iPosition = _iLength;
			break;

		// Recalibrate (FiRES: i am not 100 percent sure about this)
		case CMD_RECALIBRATE:
			{
				LOG(SERIALINTERFACE, "SI - Recalibrate");
				u8* pCalibration = reinterpret_cast<u8*>(&m_origin);
				for (int i = 0; i < (int)sizeof(SOrigin); i++)
				{
					_pBuffer[i ^ 3] = *pCalibration++;
				}				
			}
			iPosition = _iLength;
			break;

		// WII Something
		case 0xCE:
			LOG(SERIALINTERFACE, "Unknown Wii SI Command");
			break;
		
		// DEFAULT
		default:
			{
				LOG(SERIALINTERFACE, "unknown SI command     (0x%x)", command);
				PanicAlert("SI: Unknown command");
				iPosition = _iLength;
			}			
			break;
		}
	}

	return iPosition;
}

// __________________________________________________________________________________________________
// GetData
//
// return true on new data (max 7 Bytes and 6 bits ;)
//
bool 
CSIDevice_GCController::GetData(u32& _Hi, u32& _Low)
{
	SPADStatus PadStatus;
	memset(&PadStatus, 0 ,sizeof(PadStatus));
	PluginPAD::PAD_GetStatus(ISIDevice::m_iDeviceNumber, &PadStatus);

	_Hi  = (u32)((u8)PadStatus.stickY);
	_Hi |= (u32)((u8)PadStatus.stickX << 8);
	_Hi |= (u32)((u16)PadStatus.button << 16);

	_Low  = (u8)PadStatus.triggerRight;
	_Low |= (u32)((u8)PadStatus.triggerLeft << 8);
	_Low |= (u32)((u8)PadStatus.substickY << 16);
	_Low |= (u32)((u8)PadStatus.substickX << 24);
	SetMic(PadStatus.MicButton);

	// F|RES:
	// i dunno if i should force it here
	// means that the pad must be "combined" with the origin to math the "final" OSPad-Struct
	_Hi |= 0x00800000;

	return true;
}

// __________________________________________________________________________________________________
// SendCommand
//	
void
CSIDevice_GCController::SendCommand(u32 _Cmd)
{
	UCommand command(_Cmd);
	switch(command.Command)
	{
	// Costis sent it in some demos :)
	case 0x00:
		break;

	case CMD_RUMBLE:
		{
			unsigned int uType = command.Parameter1;  // 0 = stop, 1 = rumble, 2 = stop hard
			unsigned int uStrength = command.Parameter2;
			if (PluginPAD::PAD_Rumble)
				PluginPAD::PAD_Rumble(ISIDevice::m_iDeviceNumber, uType, uStrength);
		}
		break;

	default:
		{
			LOG(SERIALINTERFACE, "unknown direct command     (0x%x)", _Cmd);
			PanicAlert("SI: Unknown direct command");
		}			
		break;
	}
}

// =====================================================================================================
// --- dummy device ---
// =====================================================================================================

CSIDevice_Dummy::CSIDevice_Dummy(int _iDeviceNumber) :
	ISIDevice(_iDeviceNumber)
{}

int CSIDevice_Dummy::RunBuffer(u8* _pBuffer, int _iLength)
{
	reinterpret_cast<u32*>(_pBuffer)[0] = 0x00000000; // no device
	return 4;
}

bool CSIDevice_Dummy::GetData(u32& _Hi, u32& _Low)
{
	return false;
}

void CSIDevice_Dummy::SendCommand(u32 _Cmd)
{
}

