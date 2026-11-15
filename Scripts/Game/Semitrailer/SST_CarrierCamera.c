// A vehicle on a carrier deck is a child of the trailer. The vanilla third person vehicle camera takes the
// root parent of the seat for the vehicle (CharacterCamera3rdPersonVehicle.InitCameraData) and reads the
// camera data from it without a null check: the trailer has none, getting in and switching the view gave a
// null pointer. Here the camera keeps the trailer as root (it is what moves, OnUpdate compares against it)
// and takes the camera data from the vehicle that sits in it.

modded class CharacterCamera3rdPersonVehicle
{
	//------------------------------------------------------------------------------------------------
	override void InitCameraData()
	{
		CompartmentAccessComponent compartmentAccess = m_OwnerCharacter.GetCompartmentAccessComponent();
		if (!compartmentAccess || !compartmentAccess.IsInCompartment())
		{
			super.InitCameraData();
			return;
		}

		BaseCompartmentSlot compartment = compartmentAccess.GetCompartment();
		IEntity compartmentOwner = compartment.GetOwner();
		IEntity root = compartmentOwner.GetRootParent();
		IEntity vehicle = compartment.GetVehicle();
		if (!root || !vehicle || root == vehicle || root.FindComponent(SCR_VehicleCameraDataComponent))
		{
			super.InitCameraData();
			return;
		}

		m_pCompartment = compartment;
		m_OwnerVehicle = root;

		SCR_VehicleCameraDataComponentClass camData;
		SCR_VehicleCameraDataComponent ownerCamComp = SCR_VehicleCameraDataComponent.Cast(compartmentOwner.FindComponent(SCR_VehicleCameraDataComponent));
		if (ownerCamComp)
			camData = SCR_VehicleCameraDataComponentClass.Cast(ownerCamComp.GetComponentData(compartmentOwner));

		if (!camData || !camData.m_bOverrideVehicleSettings)
		{
			SCR_VehicleCameraDataComponent vehicleCamComp = SCR_VehicleCameraDataComponent.Cast(vehicle.FindComponent(SCR_VehicleCameraDataComponent));
			if (vehicleCamComp)
				camData = SCR_VehicleCameraDataComponentClass.Cast(vehicleCamComp.GetComponentData(vehicle));
		}

		m_pCameraPivot = null;
		if (camData)
		{
			m_fHeight = camData.m_fHeight;
			m_fDist_Desired = camData.m_fDist_Desired;
			m_fDist_Min = camData.m_fDist_Min;
			m_fDist_Max = camData.m_fDist_Max;
			m_fFOV_SpeedAdjustMax = camData.m_fFOV_SpeedAdjustMax;
			m_fBobScale = camData.m_fBobScale;
			m_fShakeScale = camData.m_fShakeScale;
			m_fSpeedMax = camData.m_fSpeedMax;
			m_pCameraAimpointData = camData.m_pCameraAimpointData;
			m_pCameraAlignData = camData.m_pCameraAlignData;
			m_fRollFactor = camData.m_3rdPersonRollFactor;
			m_fPitchFactor = camData.m_3rdPersonPitchFactor;
			m_fAngleThirdPerson = camData.m_fAngleThirdPerson * Math.DEG2RAD;
			m_pCameraPivot = camData.m_pPivot;
			m_bUseNoParent = camData.m_bNoParent;
			m_bForceBasedCamera = camData.m_UseForceBased_3PVCamera;
			m_fCameraMovementForceFactor = camData.m_MovementForceFactor;
			m_fCameraMovementForceThreshold = camData.m_MovementForceThreshold;
			m_fCameraSpringForceFactor = camData.m_SpringForceFactor;
			m_bDollyZoomEffect = camData.m_3PVDollyZoomEffect;
			m_fCameraHorizontalLag = camData.m_HorizontalSpringLag;
			m_fCameraHorizontalLagMax = camData.m_HorizontalSpringLagMaxAngle;

			m_fCamDist = m_fDist_Desired;
			m_fCamDistTarget = m_fDist_Desired;
			m_fZoomTargetWidth = m_fCamDist * 2 * Math.Tan(0.5 * m_fFOV * Math.DEG2RAD);
		}

		// camera centre of the vehicle, in the space of the trailer it rides on
		vector centre;
		if (m_pCameraPivot)
		{
			vector pivot[4];
			m_pCameraPivot.GetLocalTransform(pivot);
			centre = pivot[3];
		}
		else if (vehicle.GetPhysics())
		{
			centre = vehicle.GetPhysics().GetCenterOfMass();
		}
		else
		{
			vector mins, maxs;
			vehicle.GetBounds(mins, maxs);
			centre = (mins + maxs) * 0.5;
		}

		vector toRoot[4];
		if (EntityUtils.GetChildToAncestorTransform(vehicle, root, toRoot))
			centre = centre.Multiply4(toRoot);

		m_vCameraCenter = centre;
	}
}
