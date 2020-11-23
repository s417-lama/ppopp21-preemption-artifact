files = [
    "~/playground/results/measure_latest/*.out",
]

opts = [
    {"managed"},
]

Enum.zip(files, opts)
|> Enum.map(fn {file, opt} ->
    {title} = opt
    {xs, ys} =
        file
        |> Path.expand()
        |> Path.wildcard()
        |> Enum.map(fn path ->
            [intvl] =
                File.stream!(path)
                |> Stream.filter(fn line -> String.contains?(line, "Interval time of preemption") end)
                |> Stream.map(&String.split/1)
                |> Stream.map(fn list -> Enum.at(list, 4) |> String.to_integer() end)
                |> Enum.to_list()
            data =
                File.stream!(path)
                |> Stream.filter(fn line -> String.contains?(line, "signal handler") end)
                |> Stream.map(&String.split/1)
                |> Stream.map(fn list -> Enum.at(list, 7) |> String.to_integer() end)
                |> Enum.to_list()
            {intvl, List.last(data)}
        end)
        |> Enum.sort()
        |> Enum.unzip()
    %{
        x:    xs,
        y:    ys,
        name: title,
        type: "scatter",
    }
end)
|> PlotlyEx.plot(%{
    width:  700,
    height: 500,
    xaxis: %{
        title: %{text: "Timer interval (nsec)"},
    },
    yaxis: %{
        title: %{text: "Time to call a signal handler (nsec)"},
    },
})
|> PlotlyEx.show()
