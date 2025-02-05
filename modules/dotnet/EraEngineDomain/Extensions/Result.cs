﻿using System.Runtime.CompilerServices;

namespace EraEngine;

public class Result<T>
{
    public T Value
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get;
    }
    public string Error
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get;
    }
    public bool IsSuccess => Error == null;

    private Result(T value, string error)
    {
        Value = value;
        Error = error;
    }

    public static Result<T> Success(T value) => new(value, null!);
    public static Result<T> Failure(string error) => new(default, error);
}
