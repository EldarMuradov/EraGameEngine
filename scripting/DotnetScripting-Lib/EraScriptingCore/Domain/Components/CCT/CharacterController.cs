﻿using System.Numerics;
using System.Runtime.InteropServices;

namespace EraScriptingCore.Domain.Components;

public enum CharacterControllerType 
{
    None,
    Box,
    Capsule
}

public abstract class CharacterController : EComponent
{
    public CharacterControllerType Type { get; protected set; }

    public virtual void Move(Vector3 position) { move(Entity.Id, position); }

    #region P/I

    [DllImport("EraScriptingCPPDecls.dll")]
    private static extern float move(int id, Vector3 position);

    #endregion
}
