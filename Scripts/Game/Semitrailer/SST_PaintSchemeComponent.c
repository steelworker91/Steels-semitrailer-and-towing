// Paint scheme of child entities. The trailer model itself gets its paint from the prefab (MeshObject
// materials), the wheels and hubs are child prefabs shared by all trailers, so their paint is swapped here.

[BaseContainerProps()]
class SST_MaterialSwap
{
	[Attribute("", UIWidgets.EditBox, "Material name in the model (as in the FBX)")]
	string m_sSource;

	[Attribute("", UIWidgets.ResourceNamePicker, "Material used instead", params: "emat")]
	ResourceName m_sTarget;

	//------------------------------------------------------------------------------------------------
	//! Model material names can carry a suffix ("Name_GUID"), the FBX name is the part in front
	bool Matches(string material)
	{
		if (material == m_sSource)
			return true;

		int len = m_sSource.Length();
		return material.Length() > len && material.Substring(0, len + 1) == m_sSource + "_";
	}
}

[ComponentEditorProps(category: "SST/Semitrailer", description: "Swaps materials on the children of the entity (wheels, hubs)")]
class SST_PaintSchemeComponentClass : ScriptComponentClass
{
}

class SST_PaintSchemeComponent : ScriptComponent
{
	[Attribute(desc: "Materials to swap on all child entities")]
	protected ref array<ref SST_MaterialSwap> m_aSwaps;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		// the child prefabs are created after their parent
		if (GetGame())
			GetGame().GetCallqueue().CallLater(Apply, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Sets the swapped materials again, e.g. after a wreck was repaired
	void Apply()
	{
		IEntity owner = GetOwner();
		if (!owner || !m_aSwaps || m_aSwaps.IsEmpty())
			return;

		IEntity child = owner.GetChildren();
		while (child)
		{
			ApplyTo(child);
			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyTo(IEntity ent)
	{
		VObject obj = ent.GetVObject();
		if (obj)
		{
			string materials[256];
			int count = obj.GetMaterials(materials);
			string remap;
			for (int i = 0; i < count; i++)
			{
				foreach (SST_MaterialSwap swap : m_aSwaps)
				{
					if (swap.Matches(materials[i]))
					{
						remap += string.Format("$remap '%1' '%2';", materials[i], swap.m_sTarget);
						break;
					}
				}
			}

			if (!remap.IsEmpty())
			{
				ent.SetObject(obj, remap);
				ent.Update();
			}
		}

		IEntity child = ent.GetChildren();
		while (child)
		{
			ApplyTo(child);
			child = child.GetSibling();
		}
	}
}
