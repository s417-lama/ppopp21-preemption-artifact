timer_intvl = 500
filenames = [
    "~/workspace/argonne/playground/data.ignore/naive_timer_#{timer_intvl}us.dat",
    "~/workspace/argonne/playground/data.ignore/managed_timer_#{timer_intvl}us.dat",
    "~/workspace/argonne/playground/data.ignore/central_timer_#{timer_intvl}us.dat",
    "~/workspace/argonne/playground/data.ignore/chain_timer_#{timer_intvl}us.dat",
    # "~/workspace/argonne/playground/data.ignore/hybrid_central_timer_#{timer_intvl}us.dat",
    # "~/workspace/argonne/playground/data.ignore/hybrid_chain_timer_#{timer_intvl}us.dat",
]
|> Enum.map(&Path.expand/1)

opts = [
    {"naive"},
    {"managed"},
    {"central"},
    {"chain"},
    # {"managed + central"},
    # {"managed + chain"},
]

datas =
    Enum.map(filenames, fn filename ->
        File.stream!(filename)
        |> Stream.map(&String.split(&1, ","))
        |> Stream.map(fn [r0, t0, _, t1, kind] ->
            {String.to_integer(r0), String.to_integer(t1) - String.to_integer(t0), String.trim(kind)}
        end)
        |> Enum.to_list()
    end)

# distribution

Enum.zip(datas, opts)
|> Enum.with_index(1)
|> Enum.map(fn {{data, opt}, i} ->
    {title} = opt
    plot_data =
        data
        |> Enum.filter(fn {_r, _t, kind} -> kind == "enter handler" end)
        |> Enum.map(fn {_r, t, _kind} -> t end)
    %{
        x:       plot_data,
        name:    title,
        type:    "histogram",
        yaxis:   "y#{i}",
        xbins:   %{size: 100},
        opacity: 0.8,
    }
end)
|> PlotlyEx.plot(%{
    grid:  %{rows: length(datas), columns: 1},
    xaxis: %{
        title:    %{text: "Time to call a signal handler (nsec)"},
        zeroline: true,
        showgrid: true,
        dtick:    1000,
        range:    [0, 30000],
    },
    width:  1400,
    height: 800
})
|> PlotlyEx.show()

# each rank

Enum.zip(datas, opts)
|> Enum.with_index(1)
|> Enum.map(fn {{data, opt}, i} ->
    {title} = opt
    {rs, ts} =
        data
        |> Enum.filter(fn {_r, _t, kind} -> kind == "enter handler" end)
        |> Enum.map(fn {r, t, _kind} -> {r, t} end)
        |> Enum.unzip()
    %{
        x:      rs,
        y:      ts,
        name:   title,
        type:   "box",
        yaxis:  "y#{i}",
        marker: %{size: 2},
        # marker: %{size: 2, color: "rgb(214, 39, 40)"},
    }
end)
|> List.flatten()
|> PlotlyEx.plot(%{
    grid:   %{rows: length(datas), columns: 1},
    xaxis: %{
        title: %{text: "rank of ES"},
        dtick: 1,
    },
    yaxis:  %{type: "log", range: [2.9, 5.5]},
    yaxis2: %{type: "log", range: [2.9, 5.5]},
    yaxis3: %{type: "log", range: [2.9, 5.5]},
    yaxis4: %{type: "log", range: [2.9, 5.5]},
    yaxis5: %{type: "log", range: [2.9, 5.5]},
    yaxis6: %{type: "log", range: [2.9, 5.5]},
    width:  1400,
    height: 1000,
})
|> PlotlyEx.show()
